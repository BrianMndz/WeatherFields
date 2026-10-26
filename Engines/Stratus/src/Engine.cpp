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

} // namespace

const wf::ParamTable& Engine::Params() const noexcept {
    return kTable;
}

std::size_t Engine::MemoryBytes(float sampleRate, std::size_t) const noexcept {
    return kChannels * ChannelBytes(sampleRate);
}

wf::PrepareStatus Engine::Prepare(float sampleRate, std::size_t, MemoryBlock memory) noexcept {
    // Silent until this succeeds: a failure below must not leave Process()
    // running on pointers into the previous block.
    prepared_ = false;

    // The rate first: MemoryBytes() converts 30 s of it to a frame count.
    if (!SupportedRate(sampleRate)) return PrepareStatus::UnsupportedSampleRate;
    const PrepareStatus memoryStatus = CheckMemory(memory, MemoryBytes(sampleRate, 0));
    if (memoryStatus != PrepareStatus::Ok) return memoryStatus;

    // Left channel, then right, each aligned. The buffer is not cleared: the
    // first take overwrites what it plays, and nothing reads past length_.
    auto* bytes = static_cast<unsigned char*>(memory.data);
    left_ = reinterpret_cast<float*>(bytes);
    right_ = reinterpret_cast<float*>(bytes + ChannelBytes(sampleRate));
    capacity_ = FramesFor(sampleRate);

    state_ = State::kEmpty;
    length_ = 0;
    position_ = 0;

    loopLevelDb_ = 0.0f;
    loopGain_ = 1.0f;
    thru_ = true;

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
                    loopGain_ = loopLevelDb_ <= kLoopLevelOffDb
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
        float outputL = thru_ ? inputL : 0.0f;
        float outputR = thru_ ? inputR : 0.0f;

        // Check each frame: finishing a full take changes the state mid-block.
        switch (state_) {
            case State::kEmpty:
            case State::kStopped: break;
            case State::kRecording:
                left_[position_] = inputL;
                right_[position_] = inputR;
                if (++position_ == capacity_) FinishFirstTake(State::kPlaying);
                break;
            case State::kPlaying:
            case State::kOverdubbing:
                // Hear the old loop before adding input, so Thru sounds it once.
                outputL += left_[position_] * loopGain_;
                outputR += right_[position_] * loopGain_;
                if (state_ == State::kOverdubbing) {
                    left_[position_] += inputL;
                    right_[position_] += inputR;
                }
                if (++position_ == length_) position_ = 0;
                break;
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
            if (!isPlayOn) state_ = State::kStopped;
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
    length_ = 0;
    position_ = 0;
    state_ = State::kEmpty;
}

void Engine::FinishFirstTake(State nextState) noexcept {
    // During the first take, position_ counts the frames written.
    length_ = position_;
    position_ = 0;
    // A press and release before any audio leaves no loop to play.
    state_ = length_ == 0 ? State::kEmpty : nextState;
}

bool Engine::PlayOn() const noexcept {
    return state_ == State::kRecording || state_ == State::kPlaying ||
           state_ == State::kOverdubbing;
}

bool Engine::RecordOn() const noexcept {
    return state_ == State::kRecording || state_ == State::kOverdubbing;
}
