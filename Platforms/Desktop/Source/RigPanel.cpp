#include "RigPanel.h"

#include <algorithm>

namespace wf {
namespace desktop {

namespace {

constexpr int kMargin = 12;
constexpr int kGap = 8;
constexpr int kTitleHeight = 28;
constexpr int kLabelHeight = 18;
constexpr int kKnobWidth = 96;
constexpr int kKnobHeight = 96;
constexpr int kMenuWidth = 160;
constexpr int kButtonWidth = 96;
constexpr int kRowControlHeight = 28;  // menus and buttons
constexpr int kRefreshHz = 30;

int ItemWidth(ParamKind kind) {
    switch (kind) {
        case ParamKind::Continuous: return kKnobWidth;
        case ParamKind::Choice: return kMenuWidth;
        case ParamKind::Toggle:
        case ParamKind::Trigger: return kButtonWidth;
    }
    return kKnobWidth;
}

int ItemHeight(ParamKind kind) {
    return kLabelHeight + (kind == ParamKind::Continuous ? kKnobHeight : kRowControlHeight);
}

}  // namespace

RigPanel::RigPanel(AudioHost& host) : host_(host) {
    const Rig* rig = host_.GetRig();
    if (rig == nullptr) return;

    for (std::size_t slot = 0; slot < rig->SlotCount(); ++slot) {
        auto title = std::make_unique<juce::Label>(juce::String(), rig->Slot(slot)->Name());
        title->setFont(juce::FontOptions(18.0f, juce::Font::bold));
        addAndMakeVisible(*title);
        titles_.push_back(std::move(title));
    }

    const std::vector<AudioHost::ParamRef>& params = host_.Params();
    for (std::size_t index = 0; index < params.size(); ++index) {
        const ParamSpec& spec = *params[index].spec;
        auto control = std::make_unique<Control>();
        control->index = index;
        control->slot = params[index].slot;
        control->kind = spec.kind;
        control->label = std::make_unique<juce::Label>(juce::String(), spec.name);
        control->label->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*control->label);

        switch (spec.kind) {
            case ParamKind::Continuous: {
                auto slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                             juce::Slider::TextBoxBelow);
                slider->setRange(spec.min, spec.max);
                slider->setNumDecimalPlacesToDisplay(2);
                if (spec.unit[0] != '\0') slider->setTextValueSuffix(juce::String(" ") + spec.unit);
                slider->setDoubleClickReturnValue(true, spec.def);
                slider->setValue(spec.def, juce::dontSendNotification);
                juce::Slider* raw = slider.get();
                slider->onValueChange = [this, index, raw] {
                    host_.SendFromGui(index, static_cast<float>(raw->getValue()));
                };
                control->slider = std::move(slider);
                addAndMakeVisible(*control->slider);
                break;
            }
            case ParamKind::Choice: {
                auto menu = std::make_unique<juce::ComboBox>();
                const int count = static_cast<int>(spec.max) + 1;
                for (int i = 0; i < count; ++i) menu->addItem(spec.labels[i], i + 1);
                menu->setSelectedItemIndex(static_cast<int>(spec.def), juce::dontSendNotification);
                juce::ComboBox* raw = menu.get();
                menu->onChange = [this, index, raw] {
                    host_.SendFromGui(index, static_cast<float>(raw->getSelectedItemIndex()));
                };
                control->menu = std::move(menu);
                addAndMakeVisible(*control->menu);
                break;
            }
            case ParamKind::Toggle:
            case ParamKind::Trigger: {
                const bool toggle = spec.kind == ParamKind::Toggle;
                auto button = std::make_unique<juce::TextButton>(toggle ? "Off" : spec.name);
                juce::TextButton* raw = button.get();
                if (toggle) {
                    button->setClickingTogglesState(true);
                    button->setToggleState(spec.def != 0.0f, juce::dontSendNotification);
                    button->onClick = [this, index, raw] {
                        host_.SendFromGui(index, raw->getToggleState() ? 1.0f : 0.0f);
                    };
                } else {
                    control->label->setText(juce::String(), juce::dontSendNotification);
                    button->onClick = [this, index] { host_.SendFromGui(index, 1.0f); };
                }
                control->button = std::move(button);
                addAndMakeVisible(*control->button);
                break;
            }
        }
        controls_.push_back(std::move(control));
    }

    timerCallback();
    startTimerHz(kRefreshHz);
}

RigPanel::~RigPanel() {
    stopTimer();
}

int RigPanel::HeightFor(int width) const {
    return Layout(width, false);
}

void RigPanel::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void RigPanel::resized() {
    Layout(getWidth(), true);
}

// Each slot's title, then its controls left to right, wrapping at `width`.
int RigPanel::Layout(int width, bool apply) const {
    int y = kMargin;
    std::size_t next = 0;
    for (std::size_t slot = 0; slot < titles_.size(); ++slot) {
        if (apply) titles_[slot]->setBounds(kMargin, y, width - 2 * kMargin, kTitleHeight);
        y += kTitleHeight + kGap;

        int x = kMargin;
        int rowHeight = 0;
        for (; next < controls_.size() && controls_[next]->slot == slot; ++next) {
            const Control& control = *controls_[next];
            const int w = ItemWidth(control.kind);
            const int h = ItemHeight(control.kind);
            if (x > kMargin && x + w > width - kMargin) {
                x = kMargin;
                y += rowHeight + kGap;
                rowHeight = 0;
            }
            if (apply) {
                control.label->setBounds(x, y, w, kLabelHeight);
                const juce::Rectangle<int> area(x, y + kLabelHeight, w, h - kLabelHeight);
                if (control.slider != nullptr) control.slider->setBounds(area);
                if (control.menu != nullptr) control.menu->setBounds(area);
                if (control.button != nullptr) control.button->setBounds(area);
            }
            x += w + kGap;
            rowHeight = std::max(rowHeight, h);
        }
        y += rowHeight + 2 * kGap;
    }
    return y + kMargin;
}

// Retries what the GUI's input refused, then shows the published values,
// leaving alone a control the user is holding or whose value still waits.
void RigPanel::timerCallback() {
    host_.FlushGui();
    for (const auto& control : controls_) {
        if (host_.IsPending(control->index)) continue;
        const float value = host_.PublishedValue(control->index);
        if (control->slider != nullptr) {
            if (!control->slider->isMouseButtonDown() && control->slider->getValue() != value) {
                control->slider->setValue(value, juce::dontSendNotification);
            }
        } else if (control->menu != nullptr) {
            const int item = static_cast<int>(value);
            if (control->menu->getSelectedItemIndex() != item) {
                control->menu->setSelectedItemIndex(item, juce::dontSendNotification);
            }
        } else if (control->button != nullptr && control->kind == ParamKind::Toggle) {
            const bool on = value != 0.0f;
            if (control->button->getToggleState() != on) {
                control->button->setToggleState(on, juce::dontSendNotification);
            }
            control->button->setButtonText(on ? "On" : "Off");
        }
    }
}

}  // namespace desktop
}  // namespace wf
