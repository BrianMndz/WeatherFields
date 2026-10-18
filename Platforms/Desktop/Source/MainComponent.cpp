#include "MainComponent.h"

#include "RigPanel.h"
#include "Rigs.h"

namespace wf {
namespace desktop {

namespace {

constexpr int kBarHeight = 40;
constexpr int kStatusHz = 4;
constexpr const char* kDeviceStateKey = "audioDevice";

const char* Describe(PrepareStatus status) {
    switch (status) {
        case PrepareStatus::Ok: return "ok";
        case PrepareStatus::MemoryTooSmall: return "memory too small";
        case PrepareStatus::MemoryMisaligned: return "memory misaligned";
        case PrepareStatus::UnsupportedSampleRate: return "unsupported sample rate";
        case PrepareStatus::UnsupportedBlockSize: return "unsupported block size";
    }
    return "unknown";
}

}  // namespace

MainComponent::MainComponent() {
    juce::PropertiesFile::Options options;
    options.applicationName = "WeatherFields";
    options.folderName = "WeatherFields";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    properties_.setStorageParameters(options);

    // Stereo in and out; the saved setup if there is one, else the defaults.
    const std::unique_ptr<juce::XmlElement> saved =
        properties_.getUserSettings()->getXmlValue(kDeviceStateKey);
    deviceError_ = devices_.initialise(2, 2, saved.get(), true);
    devices_.addChangeListener(this);

    std::size_t rigCount = 0;
    const RigEntry* rigs = Rigs(rigCount);
    for (std::size_t i = 0; i < rigCount; ++i) rigMenu_.addItem(rigs[i].name, static_cast<int>(i) + 1);
    rigMenu_.onChange = [this] {
        const int item = rigMenu_.getSelectedItemIndex();
        if (item >= 0) LoadRig(static_cast<std::size_t>(item));
    };
    addAndMakeVisible(rigMenu_);

    audioButton_.onClick = [this] { ShowAudioSettings(); };
    addAndMakeVisible(audioButton_);

    status_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status_);

    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    rigMenu_.setSelectedItemIndex(0, juce::sendNotificationSync);  // loads the first rig

    setSize(640, 640);
    timerCallback();
    startTimerHz(kStatusHz);
}

MainComponent::~MainComponent() {
    // Its selector unregisters from devices_ when deleted.
    delete audioDialog_.getComponent();
    stopTimer();
    devices_.removeChangeListener(this);
    viewport_.setViewedComponent(nullptr, false);
    view_.reset();
}

void MainComponent::LoadRig(std::size_t index) {
    std::size_t rigCount = 0;
    const RigEntry* rigs = Rigs(rigCount);
    if (index >= rigCount) return;

    // The view points into the old rig's tables, so it goes first. The old
    // rig goes before the new one exists: Clouds allows one instance at a time.
    viewport_.setViewedComponent(nullptr, false);
    view_.reset();
    host_.SetRig(nullptr);
    host_.SetRig(rigs[index].make());

    if (rigs[index].makeView != nullptr) {
        view_ = rigs[index].makeView(host_);
    } else {
        view_ = std::make_unique<RigPanel>(host_);
    }
    viewport_.setViewedComponent(view_.get(), false);
    resized();
}

void MainComponent::ShowAudioSettings() {
    if (audioDialog_ != nullptr) {
        audioDialog_->toFront(true);
        return;
    }

    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        devices_, 0, 2, 1, 2, false, false, true, false);
    selector->setSize(520, 420);

    juce::DialogWindow::LaunchOptions dialog;
    dialog.content.setOwned(selector.release());
    dialog.dialogTitle = "Audio settings";
    dialog.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    dialog.useNativeTitleBar = true;
    dialog.resizable = true;
    audioDialog_ = dialog.launchAsync();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    // The device setup changed: keep it for the next run.
    if (const std::unique_ptr<juce::XmlElement> state = devices_.createStateXml()) {
        properties_.getUserSettings()->setValue(kDeviceStateKey, state.get());
    } else {
        properties_.getUserSettings()->removeValue(kDeviceStateKey);
    }
    properties_.saveIfNeeded();
}

void MainComponent::timerCallback() {
    const AudioHost::Status status = host_.GetStatus();
    juce::String text;
    if (!status.running) {
        text = deviceError_.isNotEmpty() ? "No audio device: " + deviceError_ : juce::String("No audio device");
    } else {
        text << juce::String(status.sampleRate / 1000.0, 1) << " kHz, " << status.blockSize << " frames, CPU "
             << juce::String(juce::roundToInt(devices_.getCpuUsage() * 100.0)) << "%";
        if (status.prepare != PrepareStatus::Ok && host_.GetRig() != nullptr) {
            text << "  |  " << host_.GetRig()->Name() << " silent: " << Describe(status.prepare);
        }
    }
    if (status.overflows != 0 || status.dropped != 0) {
        text << "  |  GUI events overflowed " << static_cast<int>(status.overflows) << ", dropped "
             << static_cast<int>(status.dropped);
    }
    status_.setText(text, juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized() {
    juce::Rectangle<int> area = getLocalBounds();
    juce::Rectangle<int> bar = area.removeFromTop(kBarHeight).reduced(8, 6);
    rigMenu_.setBounds(bar.removeFromLeft(160));
    bar.removeFromLeft(8);
    audioButton_.setBounds(bar.removeFromLeft(90));
    bar.removeFromLeft(8);
    status_.setBounds(bar);

    viewport_.setBounds(area);
    if (view_ != nullptr) {
        const int width = viewport_.getMaximumVisibleWidth();
        view_->setSize(width, view_->HeightFor(width, viewport_.getMaximumVisibleHeight()));
    }
}

}  // namespace desktop
}  // namespace wf
