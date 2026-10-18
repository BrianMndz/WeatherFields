#pragma once

#include <cstddef>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioHost.h"
#include "RigPanel.h"

namespace wf {
namespace desktop {

// The desktop app's window: a rig menu, the audio settings, a status line
// and the generated panel of the current rig. The audio device setup is
// saved between runs.
class MainComponent final : public juce::Component, private juce::Timer, private juce::ChangeListener {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void LoadRig(std::size_t index);
    void ShowAudioSettings();
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::ApplicationProperties properties_;
    juce::AudioDeviceManager devices_;
    AudioHost host_{devices_};  // after devices_, so it detaches first
    juce::String deviceError_;

    juce::ComboBox rigMenu_;
    juce::TextButton audioButton_{"Audio..."};
    juce::Label status_;
    juce::Viewport viewport_;
    std::unique_ptr<RigPanel> panel_;
};

}  // namespace desktop
}  // namespace wf
