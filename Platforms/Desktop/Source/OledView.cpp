#include "OledView.h"

#include <iterator>
#include <utility>

namespace wf {
namespace desktop {

namespace {

// The board's rate (WF_OLED_FPS in Platforms/Bela/Host/BelaHost.cpp), which
// its I2C bus bounds: a full frame takes about 35.5 ms to send.
constexpr int kFramesPerSecond = 25;
constexpr int kDefaultScale = 4;
constexpr int kMargin = 16;

const juce::Colour kOff(0xff050607);
const juce::Colour kBezel(0xff101113);
// Blue first: the board's panel. Then white and yellow.
const juce::Colour kLit[] = {juce::Colour(0xff4fc3ff), juce::Colour(0xffe6f0ff), juce::Colour(0xffffd23a)};

}  // namespace

float OledView::HostReadout::Value(std::size_t slot, ParamId id) const noexcept {
    const std::size_t index = host_.FindParam(slot, id);
    return index != AudioHost::kNoParam ? host_.PublishedValue(index) : 0.0f;
}

oled::RigReadout::Focus OledView::HostReadout::ViewFocus() const noexcept {
    const std::size_t index = host_.ViewFocus();
    if (index == AudioHost::kNoParam) return Focus{false, 0, 0};
    const AudioHost::ParamRef& param = host_.Params()[index];
    return Focus{true, param.slot, param.spec->id};
}

OledView::OledView(const AudioHost& host)
    : readout_(host),
      image_(juce::Image::RGB, oled::kWidth, oled::kHeight, true),
      startMs_(juce::Time::getMillisecondCounter()) {
    setSize(oled::kWidth * kDefaultScale + 2 * kMargin, oled::kHeight * kDefaultScale + 2 * kMargin);
    Render();
    startTimerHz(kFramesPerSecond);
}

void OledView::SetScreen(std::unique_ptr<oled::Screen> screen) {
    screen_ = std::move(screen);
    Render();
    repaint();
}

void OledView::timerCallback() {
    Render();
    repaint();
}

void OledView::Render() {
    oled::Screen& screen = screen_ != nullptr ? *screen_ : testPattern_;
    screen.Draw(display_.U8g2(), juce::Time::getMillisecondCounter() - startMs_, readout_);

    const juce::Image::BitmapData pixels(image_, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < oled::kHeight; ++y) {
        for (int x = 0; x < oled::kWidth; ++x) {
            pixels.setPixelColour(x, y, display_.Pixel(x, y) ? kLit[colour_] : kOff);
        }
    }
}

void OledView::paint(juce::Graphics& g) {
    g.fillAll(kBezel);
    const int scale = juce::jmax(1, juce::jmin((getWidth() - 2 * kMargin) / oled::kWidth,
                                               (getHeight() - 2 * kMargin) / oled::kHeight));
    const juce::Rectangle<int> screen =
        juce::Rectangle<int>(oled::kWidth * scale, oled::kHeight * scale).withCentre(getLocalBounds().getCentre());
    // Nearest neighbour: each panel pixel stays a sharp square.
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImage(image_, screen.toFloat(), juce::RectanglePlacement::stretchToFit);
}

void OledView::mouseDown(const juce::MouseEvent&) {
    colour_ = (colour_ + 1) % static_cast<int>(std::size(kLit));
    Render();
    repaint();
}

OledWindow::OledWindow(const AudioHost& host)
    : DocumentWindow("OLED", kBezel, juce::DocumentWindow::closeButton), view_(new OledView(host)) {
    setUsingNativeTitleBar(true);
    setContentOwned(view_, true);
    setResizable(true, false);
    setResizeLimits(oled::kWidth + 2 * kMargin, oled::kHeight + 2 * kMargin, 4096, 4096);
}

void OledWindow::closeButtonPressed() {
    setVisible(false);
}

}  // namespace desktop
}  // namespace wf
