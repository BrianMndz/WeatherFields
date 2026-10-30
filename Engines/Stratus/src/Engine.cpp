#include "wf/stratus/Engine.h"

#include <cmath>

#include "wf/Dsp.h"

namespace {

using wf::stratus::Engine;
using wf::ParamKind;
using wf::ParamSpec;

constexpr float kMaxSeconds = 30.0f; // 60 once tested
constexpr std::size_t kChannels = 2;
// Above any real host, and low enough that the sizes cannot overflow a 32-bit
// std::size_t (Bela): 192 kHz needs 46 MB.
constexpr float kMaxSampleRate = 192000.0f;
constexpr double kFadeSeconds = 0.005;

// The bottom of Loop Level is silence; Handle() maps other dB values
// to gain once per event, while keeping loopLevelDb_ for readback.
constexpr float kLoopLevelOffDb = -60.0f;

const ParamSpec kSpecs[] = {
    {Engine::kPlay, "Play", ParamKind::Toggle, 0.0f, 1.0f, 0.0f, "", nullptr},
    {Engine::kRecord, "Record", ParamKind::Toggle, 0.0f, 1.0f, 0.0f, "", nullptr},
    {Engine::kClear, "Clear", ParamKind::Trigger, 0.0f, 1.0f, 0.0f, "", nullptr},
    {Engine::kLoopLevel, "Loop Level", ParamKind::Continuous, kLoopLevelOffDb, 0.0f, 0.0f, "dB",
     nullptr},
    {Engine::kThru, "Thru", ParamKind::Toggle, 0.0f, 1.0f, 1.0f, "", nullptr},
};

const wf::ParamTable kTable = wf::MakeParamTable(kSpecs);

// IsFinite rather than comparisons alone: under -ffast-math (Bela) a NaN
// may pass them.
bool SupportedRate(float sampleRate) {
    return wf::IsFinite(sampleRate) && sampleRate > 0.0f && sampleRate <= kMaxSampleRate;
}

// 0 for an unsupported rate, so MemoryBytes() is safe before Prepare()
// refuses it.
std::size_t FramesFor(float sampleRate) {
    if (!SupportedRate(sampleRate)) return 0;
    return static_cast<std::size_t>(std::ceil(kMaxSeconds * sampleRate));
}

std::size_t ChannelBytes(float sampleRate) {
    return wf::AlignMemory(FramesFor(sampleRate) * sizeof(float));
}

std::size_t FadeFramesFor(float sampleRate) {
    const auto frames = static_cast<std::size_t>(std::ceil(sampleRate * kFadeSeconds));
    // Backstop: callers have already checked SupportedRate().
    return frames == 0 ? 1 : frames;
}

std::size_t TailBytes(float sampleRate) {
    return wf::AlignMemory(FadeFramesFor(sampleRate) * sizeof(float));
}

} // namespace

const wf::ParamTable& Engine::Params() const noexcept {
    return kTable;
}

std::size_t Engine::MemoryBytes(float sampleRate, std::size_t) const noexcept {
    if (!SupportedRate(sampleRate)) return 0;
    return kChannels * (ChannelBytes(sampleRate) + TailBytes(sampleRate));
}

wf::PrepareStatus Engine::Prepare(float sampleRate, std::size_t, MemoryBlock memory) noexcept {
    // Silent until this succeeds: a failure below must not leave Process()
    // running on pointers into the previous block.
    prepared_ = false;

    // The rate first: MemoryBytes() converts 30 s of it to a frame count.
    if (!SupportedRate(sampleRate)) return PrepareStatus::UnsupportedSampleRate;
    const PrepareStatus memoryStatus = CheckMemory(memory, MemoryBytes(sampleRate, 0));
    if (memoryStatus != PrepareStatus::Ok) return memoryStatus;

    // Loop left/right, then tail scratch left/right, each aligned. No buffer
    // clearing: read heads own recorded lengths, and savedFrames bounds the
    // scratch prefix that has actually been written.
    auto* bytes = static_cast<unsigned char*>(memory.data);
    left_ = reinterpret_cast<float*>(bytes);
    right_ = reinterpret_cast<float*>(bytes + ChannelBytes(sampleRate));
    tailLeft_ = reinterpret_cast<float*>(bytes + kChannels * ChannelBytes(sampleRate));
    tailRight_ = reinterpret_cast<float*>(bytes + kChannels * ChannelBytes(sampleRate) +
                                          TailBytes(sampleRate));
    capacity_ = FramesFor(sampleRate);

    state_ = State::kEmpty;
    length_ = 0;
    position_ = 0;
    tail_ = {};

    loopLevelDb_ = 0.0f;
    loopLevelGain_ = 1.0f;
    thru_ = true;
    fadeFrames_ = FadeFramesFor(sampleRate);
    dryFadePosition_ = fadeFrames_;
    inputFadePosition_ = 0;
    loopFadePosition_ = 0;
    fadeStep_ = 1.0f / static_cast<float>(fadeFrames_);

    prepared_ = true;
    return PrepareStatus::Ok;
}

void Engine::Handle(const ControlEvent& event) noexcept {
    if (!prepared_) return;
    switch (event.type) {
        case ControlEvent::Type::Param:
            switch (event.id) {
                case kPlay: OnPlay(event.value != 0.0f); break;
                case kRecord: OnRecord(event.value != 0.0f); break;
                case kLoopLevel:
                    loopLevelDb_ = event.value;
                    loopLevelGain_ = loopLevelDb_ <= kLoopLevelOffDb
                                         ? 0.0f
                                         : std::pow(10.0f, loopLevelDb_ / 20.0f);
                    break;
                case kThru: thru_ = event.value != 0.0f; break;
                default: break;
            }
            break;
        case ControlEvent::Type::Trigger:
            if (event.id == kClear) OnClear();
            break;
        case ControlEvent::Type::NoteOn:
        case ControlEvent::Type::NoteOff:
            break;
    }
}

float Engine::ParamValue(ParamId id) const noexcept {
    switch (id) {
        case kPlay:
            return PlayOn() ? 1.0f : 0.0f;
        case kRecord:
            return RecordOn() ? 1.0f : 0.0f;
        case kLoopLevel:
            return loopLevelDb_;
        case kThru:
            return thru_ ? 1.0f : 0.0f;
        case kClear:
        default:
            return 0.0f;
    }
}

void Engine::Process(const float* inLeft, const float* inRight, float* outLeft, float* outRight,
                     std::size_t frames) noexcept {
    if (!prepared_) {
        for (std::size_t i = 0; i < frames; ++i) {
            outLeft[i] = 0.0f;
            outRight[i] = 0.0f;
        }
        return;
    }

    for (std::size_t i = 0; i < frames; ++i) {
        // Both inputs must be saved before either output is written (in-place).
        const float inputL = inLeft[i];
        const float inputR = inRight[i];
        const float dryGain = AdvanceRamp(dryFadePosition_, thru_);
        // The first take writes full input below. Only overdub punches ramp;
        // after Record off, Playing keeps writing until this gain reaches zero.
        const bool tailActive = tail_.fadePosition != 0;
        const float inputGain = AdvanceRamp(inputFadePosition_,
                                           !tailActive && state_ == State::kOverdubbing);
        float outputL = inputL * dryGain;
        float outputR = inputR * dryGain;

        if (tailActive) {
            const float gain = AdvanceRamp(tail_.fadePosition, false) * loopLevelGain_;
            const std::size_t pos = tail_.position;
            // A new take may have overwritten this prefix since Clear.
            const bool saved = tail_.discarded && pos < tail_.savedFrames;
            outputL += (saved ? tailLeft_[pos] : left_[pos]) * gain;
            outputR += (saved ? tailRight_[pos] : right_[pos]) * gain;
            if (!tail_.discarded && inputFadePosition_ != 0) {
                left_[pos] += inputL * inputGain;
                right_[pos] += inputR * inputGain;
            }
            if (++tail_.position == tail_.length) tail_.position = 0;
        }

        // Check each frame: finishing a full take changes the state mid-block.
        switch (state_) {
            case State::kEmpty:
            case State::kStopped: break;
            case State::kRecording:
                // Preserve each old sample just before the new take replaces
                // it. At most fadeFrames_ writes precede the tail's end; Clear
                // does no copying, and scratch is read only below savedFrames.
                if (tailActive && tail_.discarded && position_ == tail_.savedFrames &&
                    position_ < tail_.length && position_ < fadeFrames_) {
                    tailLeft_[position_] = left_[position_];
                    tailRight_[position_] = right_[position_];
                    ++tail_.savedFrames;
                }
                left_[position_] = inputL;
                right_[position_] = inputR;
                if (++position_ == capacity_) FinishFirstTake(State::kPlaying);
                break;
            case State::kPlaying:
            case State::kOverdubbing:
                // One read head: a restart or newly finished take waits until
                // the old tail reaches zero before regular playback resumes.
                if (tailActive) break;
                // Hear the old loop before adding input, so Thru sounds it once.
                {
                    const float gain = AdvanceRamp(loopFadePosition_, true) * loopLevelGain_;
                    outputL += left_[position_] * gain;
                    outputR += right_[position_] * gain;
                }
                if (inputFadePosition_ != 0) {
                    left_[position_] += inputL * inputGain;
                    right_[position_] += inputR * inputGain;
                }
                if (++position_ == length_) position_ = 0;
                break;
        }
        if (tailActive && tail_.fadePosition == 0) {
            if (!tail_.discarded) position_ = 0;
            // A new take owns its own position/length, even if it has already
            // finished or stopped during the old tail. Never rewind it here.
            tail_ = {};
        }
        outLeft[i] = outputL;
        outRight[i] = outputR;
    }
}

void Engine::OnRecord(bool isRecOn) noexcept {
    switch (state_) {
        case State::kEmpty:
            if (isRecOn) {
                length_ = 0;
                position_ = 0;
                loopFadePosition_ = 0;
                state_ = State::kRecording;
            }
            break;
        case State::kRecording:
            if (!isRecOn) FinishFirstTake(State::kPlaying);
            break;
        case State::kPlaying:
            if (isRecOn) state_ = State::kOverdubbing;
            break;
        case State::kOverdubbing:
            if (!isRecOn) state_ = State::kPlaying;
            break;
        case State::kStopped:
            if (isRecOn) {
                position_ = 0;
                state_ = State::kOverdubbing;
            }
            break;
    }
}

void Engine::OnPlay(bool isPlayOn) noexcept {
    switch (state_) {
        case State::kEmpty:
            // No loop to play.
            break;
        case State::kRecording:
            if (!isPlayOn) FinishFirstTake(State::kStopped);
            break;
        case State::kPlaying:
        case State::kOverdubbing:
            if (!isPlayOn) {
                BeginTail(false);
                state_ = State::kStopped;
                loopFadePosition_ = 0;
            }
            break;
        case State::kStopped:
            if (isPlayOn) {
                position_ = 0;
                state_ = State::kPlaying;
            }
            break;
    }
}

void Engine::OnClear() noexcept {
    BeginTail(true);
    if (tail_.fadePosition != 0) tail_.discarded = true;
    // The tail owns its copied length, so the main loop is empty immediately.
    length_ = 0;
    position_ = 0;
    // The discarded loop's punch must never carry into a new take.
    inputFadePosition_ = 0;
    loopFadePosition_ = 0;
    state_ = State::kEmpty;
}

void Engine::BeginTail(bool discarded) noexcept {
    // Repeated Stop/Clear events must not restart an existing fade. A new
    // take can finish while it runs, but has not played through this head yet.
    if (tail_.fadePosition != 0 || loopFadePosition_ == 0 || length_ == 0) return;
    tail_ = {position_, length_, loopFadePosition_, 0, discarded};
    loopFadePosition_ = 0;
}

float Engine::AdvanceRamp(std::size_t& position, bool on) noexcept {
    if (on) {
        if (position < fadeFrames_) ++position;
    } else {
        if (position > 0) --position;
    }
    return position == fadeFrames_ ? 1.0f : static_cast<float>(position) * fadeStep_;
}

void Engine::FinishFirstTake(State nextState) noexcept {
    // During the first take, position_ counts the frames written.
    length_ = position_;
    position_ = 0;
    // A press and release before any audio leaves no loop to play.
    state_ = length_ == 0 ? State::kEmpty : nextState;
    // First-take edges are content, smoothed by the future seam crossfade.
    loopFadePosition_ = state_ == State::kPlaying ? fadeFrames_ : 0;
}

bool Engine::PlayOn() const noexcept {
    return state_ == State::kRecording || state_ == State::kPlaying ||
           state_ == State::kOverdubbing;
}

bool Engine::RecordOn() const noexcept {
    return state_ == State::kRecording || state_ == State::kOverdubbing;
}
