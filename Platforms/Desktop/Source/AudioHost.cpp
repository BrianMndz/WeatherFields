#include "AudioHost.h"

#include <algorithm>
#include <cstring>

namespace wf {
namespace desktop {

AudioHost::AudioHost(juce::AudioDeviceManager& devices) : devices_(devices) {
    devices_.addAudioCallback(this);
}

AudioHost::~AudioHost() {
    devices_.removeAudioCallback(this);
}

void AudioHost::SetRig(std::unique_ptr<Rig> rig) {
    // Returns once no callback is running, and none starts until the
    // callback is added again.
    devices_.removeAudioCallback(this);
    guiInput_.Discard();

    rig_ = std::move(rig);
    params_.clear();
    if (rig_ != nullptr) {
        for (std::size_t slot = 0; slot < rig_->SlotCount(); ++slot) {
            const ParamTable& table = rig_->Slot(slot)->Params();
            for (std::size_t i = 0; i < table.count; ++i) params_.push_back({slot, &table.specs[i]});
        }
    }
    published_.reset(new std::atomic<float>[params_.size()]);
    for (std::size_t i = 0; i < params_.size(); ++i) {
        const ParamSpec& spec = *params_[i].spec;
        published_[i].store(spec.kind == ParamKind::Trigger ? 0.0f : spec.def, std::memory_order_relaxed);
    }
    pending_.assign(params_.size(), Pending{});
    restoreValues_ = false;

    devices_.addAudioCallback(this);  // prepares the rig, if a device is open
}

void AudioHost::SendFromGui(std::size_t index, float value) {
    if (index >= params_.size()) return;
    pending_[index] = Pending{true, value};
    FlushGui();
}

void AudioHost::FlushGui() {
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        if (!pending_[i].waiting) continue;
        if (!TryPush(i, pending_[i].value)) return;  // full: the timer retries
        pending_[i].waiting = false;
    }
}

bool AudioHost::TryPush(std::size_t index, float value) {
    const ParamRef& param = params_[index];
    const bool trigger = param.spec->kind == ParamKind::Trigger;
    return guiInput_.Push(ControlEvent{trigger ? ControlEvent::Type::Trigger : ControlEvent::Type::Param,
                                       static_cast<std::uint8_t>(param.slot), param.spec->id,
                                       trigger ? 1.0f : value});
}

AudioHost::Status AudioHost::GetStatus() const noexcept {
    return Status{running_.load(), sampleRate_.load(), blockSize_.load(), prepare_.load(),
                  guiInput_.Overflows(), guiInput_.Dropped()};
}

void AudioHost::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    const double sampleRate = device->getCurrentSampleRate();
    const int blockSize = device->getCurrentBufferSizeSamples();
    maxFrames_ = blockSize > 0 ? static_cast<std::size_t>(blockSize) : 0;
    silence_.assign(maxFrames_, 0.0f);
    scratchLeft_.assign(maxFrames_, 0.0f);
    scratchRight_.assign(maxFrames_, 0.0f);

    PrepareStatus status = PrepareStatus::Ok;
    if (rig_ != nullptr) {
        const float rate = static_cast<float>(sampleRate);
        const std::size_t bytes = rig_->MemoryBytes(rate, maxFrames_);
        memory_.assign(bytes + kMemoryAlignment, 0);
        const auto address = reinterpret_cast<std::uintptr_t>(memory_.data());
        const std::size_t offset = (kMemoryAlignment - address % kMemoryAlignment) % kMemoryAlignment;
        status = rig_->Prepare(rate, maxFrames_, MemoryBlock{memory_.data() + offset, bytes});

        if (status == PrepareStatus::Ok) {
            // A device restart (new rate or block size) re-prepares the rig,
            // which resets it; the values it had come back as events, through
            // the same validation as the GUI's.
            if (restoreValues_) {
                for (std::size_t i = 0; i < params_.size(); ++i) {
                    const ParamRef& param = params_[i];
                    if (param.spec->kind == ParamKind::Trigger) continue;
                    rig_->Handle(ControlEvent{ControlEvent::Type::Param,
                                              static_cast<std::uint8_t>(param.slot), param.spec->id,
                                              published_[i].load(std::memory_order_relaxed)});
                }
            }
            restoreValues_ = true;
            Publish();
        }
    }

    sampleRate_.store(sampleRate);
    blockSize_.store(blockSize);
    prepare_.store(status);
    running_.store(true);
}

void AudioHost::audioDeviceStopped() {
    running_.store(false);
}

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                 int numInputChannels,
                                                 float* const* outputChannelData,
                                                 int numOutputChannels, int numSamples,
                                                 const juce::AudioIODeviceCallbackContext&) {
    const std::size_t frames = numSamples > 0 ? static_cast<std::size_t>(numSamples) : 0;

    if (rig_ != nullptr && maxFrames_ != 0) {
        guiInput_.Drain(*rig_, kMaxGuiEventsPerCallback);
        Publish();

        // The rig always gets valid inputs: silence without an input, the
        // left channel twice from a mono one.
        const float* inLeft = numInputChannels > 0 ? inputChannelData[0] : nullptr;
        const float* inRight = numInputChannels > 1 ? inputChannelData[1] : inLeft;

        // Into scratch buffers, so the device's buffers never alias the rig's
        // in ways Engine::Process() does not allow; in pieces of at most the
        // prepared size, should a device call back with more.
        for (std::size_t done = 0; done < frames;) {
            const std::size_t count = std::min(frames - done, maxFrames_);
            rig_->Process(inLeft != nullptr ? inLeft + done : silence_.data(),
                          inRight != nullptr ? inRight + done : silence_.data(),
                          scratchLeft_.data(), scratchRight_.data(), count);
            if (numOutputChannels > 0 && outputChannelData[0] != nullptr) {
                std::memcpy(outputChannelData[0] + done, scratchLeft_.data(), count * sizeof(float));
            }
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr) {
                std::memcpy(outputChannelData[1] + done, scratchRight_.data(), count * sizeof(float));
            }
            done += count;
        }
    }

    const int firstSilent = rig_ != nullptr && maxFrames_ != 0 ? 2 : 0;
    for (int channel = firstSilent; channel < numOutputChannels; ++channel) {
        if (outputChannelData[channel] != nullptr) {
            std::memset(outputChannelData[channel], 0, frames * sizeof(float));
        }
    }
}

// After the callback's events, so the panel sees the changes they caused.
void AudioHost::Publish() noexcept {
    const Rig& rig = *rig_;
    for (std::size_t i = 0; i < params_.size(); ++i) {
        const ParamRef& param = params_[i];
        published_[i].store(rig.Slot(param.slot)->ParamValue(param.spec->id), std::memory_order_relaxed);
    }
}

}  // namespace desktop
}  // namespace wf
