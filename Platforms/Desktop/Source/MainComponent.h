#pragma once

#include <cstddef>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioHost.h"
#include "OledView.h"
#include "RigView.h"

namespace wf {
namespace desktop {

// The desktop app's window: a rig menu, the audio settings, the emulated
// OLED, a status line and the current rig's view: its own, or the panel
// generated from its parameter tables. The audio device setup is saved
// between runs.
class MainComponent final : public juce::Component, private juce::Timer, private juce::ChangeListener {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void LoadRig(std::size_t index);
    void ShowAudioSettings();
    void ShowOled();
    std::unique_ptr<oled::Screen> MakeScreen() const;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::ApplicationProperties properties_;
    juce::AudioDeviceManager devices_;
    AudioHost host_{devices_};  // after devices_, so it detaches first
    juce::String deviceError_;

    juce::ComboBox rigMenu_;
    juce::TextButton audioButton_{"Audio..."};
    juce::TextButton oledButton_{"OLED..."};
    juce::Label status_;
    juce::Viewport viewport_;
    std::unique_ptr<RigView> view_;

    // The audio settings window, while open. JUCE's modal manager deletes it
    // when closed; still open at quit, it would go only at shutdown, after
    // devices_, so the destructor deletes it first.
    juce::Component::SafePointer<juce::DialogWindow> audioDialog_;

    // Made when first shown; closing it hides it. It shows the current rig's
    // screen, made anew for each rig.
    std::unique_ptr<OledWindow> oledWindow_;
    std::size_t rigIndex_ = 0;
};

}  // namespace desktop
}  // namespace wf
