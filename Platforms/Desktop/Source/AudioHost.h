#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "wf/ControlInput.h"
#include "wf/Engine.h"
#include "wf/Rig.h"

namespace wf {
namespace desktop {

// Runs one wf::Rig in JUCE's audio callback, as Bela's render() will: the
// GUI's events first, then the published parameter values, then Process().
// It is the rig's only mutator; the message thread reaches the rig through
// the GUI's ControlInput and reads it back through the published values.
//
// Every parameter of every slot has a flat index, in slot order and then
// table order; the panel addresses parameters by it.
class AudioHost final : public juce::AudioIODeviceCallback {
public:
    static constexpr std::size_t kGuiInputCapacity = 256;
    // Per callback, from the GUI's input. A Clouds quality or mode event costs
    // about 0.2 ms on Bela (its Service()); on desktop it is the GUI's pace
    // that keeps these rare, and the limit that bounds a burst.
    static constexpr std::size_t kMaxGuiEventsPerCallback = 32;

    struct ParamRef {
        std::size_t slot;
        const ParamSpec* spec;  // in the rig's table, static storage
    };

    // What the status line shows. Any thread.
    struct Status {
        bool running;  // a device is open and calling back
        double sampleRate;
        int blockSize;
        PrepareStatus prepare;
        std::uint32_t overflows;  // GUI events refused by a full input
        std::uint32_t dropped;    // GUI events the rig refused
    };

    explicit AudioHost(juce::AudioDeviceManager& devices);
    ~AudioHost() override;

    AudioHost(const AudioHost&) = delete;
    AudioHost& operator=(const AudioHost&) = delete;

    // Message thread. Replaces the rig (null for none) by the procedure in
    // wf/Rig.h: detach from the device, discard queued events, swap, then
    // reattach, which prepares the new rig for the device's rate and block.
    void SetRig(std::unique_ptr<Rig> rig);

    // Message thread; null when there is none.
    const Rig* GetRig() const noexcept { return rig_.get(); }

    // Message thread. The rig's parameters by flat index.
    const std::vector<ParamRef>& Params() const noexcept { return params_; }

    // Message thread. Sends a parameter's value (ignored for a Trigger) from
    // the GUI. When the input is full, it keeps the latest value per
    // parameter and FlushGui() sends it later; a newer value replaces it.
    void SendFromGui(std::size_t index, float value);

    // Message thread, from a timer: sends what SendFromGui() could not.
    void FlushGui();

    // Message thread. Whether a value from SendFromGui() still waits.
    bool IsPending(std::size_t index) const noexcept { return pending_[index].waiting; }

    // Message thread. The value the audio thread published for `index` after
    // its last callback's events.
    float PublishedValue(std::size_t index) const noexcept {
        return published_[index].load(std::memory_order_relaxed);
    }

    Status GetStatus() const noexcept;

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    struct Pending {
        bool waiting = false;
        float value = 0.0f;
    };

    bool TryPush(std::size_t index, float value);
    void Publish() noexcept;

    juce::AudioDeviceManager& devices_;

    // The rig's arena, declared before the rig so that it outlives it.
    // Resized in audioDeviceAboutToStart(), just before Prepare().
    std::vector<unsigned char> memory_;

    // Owned by the message thread; used by the audio thread only while
    // attached.
    std::unique_ptr<Rig> rig_;
    std::vector<ParamRef> params_;
    std::unique_ptr<std::atomic<float>[]> published_;
    std::vector<Pending> pending_;
    ControlInput<kGuiInputCapacity> guiInput_;

    // Set up in audioDeviceAboutToStart(), which never runs during a
    // callback; read by the audio thread.
    std::vector<float> silence_;
    std::vector<float> scratchLeft_;
    std::vector<float> scratchRight_;
    std::size_t maxFrames_ = 0;
    bool restoreValues_ = false;  // the next Prepare() replays the published values

    std::atomic<bool> running_{false};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> blockSize_{0};
    std::atomic<PrepareStatus> prepare_{PrepareStatus::Ok};
};

}  // namespace desktop
}  // namespace wf
