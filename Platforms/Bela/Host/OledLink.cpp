#include "OledLink.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "wf/oled/TestPattern.h"

namespace wf {
namespace bela {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::seconds kReportInterval(5);
constexpr std::chrono::milliseconds kRetryInterval(1000);
constexpr std::chrono::milliseconds kStopPoll(50);

double Milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

// Frame timings between two reports.
struct Stats {
    int frames = 0;
    Clock::duration draw{};
    Clock::duration drawMax{};
    Clock::duration send{};
    Clock::duration sendMax{};

    void Add(Clock::duration drawTime, Clock::duration sendTime) {
        ++frames;
        draw += drawTime;
        drawMax = std::max(drawMax, drawTime);
        send += sendTime;
        sendMax = std::max(sendMax, sendTime);
    }

    void Print(Clock::duration elapsed) const {
        if (frames == 0) return;
        std::printf("OLED: %.1f fps; send %.1f ms mean, %.1f max; draw %.2f ms mean, %.2f max\n",
                    frames / std::chrono::duration<double>(elapsed).count(), Milliseconds(send) / frames,
                    Milliseconds(sendMax), Milliseconds(draw) / frames, Milliseconds(drawMax));
    }
};

}  // namespace

OledLink::OledLink() : display_(I2cPanel::Byte, I2cPanel::GpioAndDelay) {
    panel_.Attach(u8g2_GetU8x8(display_.U8g2()));
}

bool OledLink::Start(int bus, int address, int framesPerSecond) {
    if (!panel_.Open(bus, address)) {
        std::fprintf(stderr, "OLED: cannot open I2C bus %d at 0x%02x: %s; running without it\n", bus, address,
                     std::strerror(panel_.TakeError()));
        return false;
    }
    std::printf("OLED: I2C bus %d, address 0x%02x, up to %d frames a second\n", bus, address, framesPerSecond);
    running_ = true;
    thread_ = std::thread([this, framesPerSecond] { Run(framesPerSecond); });
    return true;
}

void OledLink::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void OledLink::Run(int framesPerSecond) {
    u8g2_t* u8g2 = display_.U8g2();
    const Clock::duration period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::microseconds(1000000 / std::max(1, framesPerSecond)));
    const Clock::time_point start = Clock::now();

    // Sleeps until `until`, waking to see whether Stop() was called.
    auto wait = [this](Clock::time_point until) {
        while (running_ && Clock::now() < until) {
            std::this_thread::sleep_until(std::min(until, Clock::now() + kStopPoll));
        }
    };

    bool panelOn = false;  // initialised and showing frames
    bool reported = false; // the current failure has been printed
    Stats stats;
    Clock::time_point reportStart = start;
    Clock::time_point next = start;

    while (running_) {
        if (!panelOn) {
            // Also after a failure: a panel that lost power has lost its setup.
            u8g2_InitDisplay(u8g2);
            u8g2_SetPowerSave(u8g2, 0);
        }

        const Clock::time_point drawStart = Clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(drawStart - start).count();
        oled::DrawTestPattern(u8g2, static_cast<std::uint32_t>(ms));
        const Clock::time_point sendStart = Clock::now();
        u8g2_SendBuffer(u8g2);
        const Clock::time_point sent = Clock::now();

        if (const int error = panel_.TakeError()) {
            if (!reported) {
                std::fprintf(stderr, "OLED: the panel does not answer: %s; retrying every second\n",
                             std::strerror(error));
                reported = true;
            }
            panelOn = false;
            stats = Stats();
            wait(sent + kRetryInterval);
            reportStart = next = Clock::now();
            continue;
        }
        if (!panelOn && reported) std::printf("OLED: the panel answers again\n");
        panelOn = true;
        reported = false;

        stats.Add(sendStart - drawStart, sent - sendStart);
        if (sent - reportStart >= kReportInterval) {
            stats.Print(sent - reportStart);
            stats = Stats();
            reportStart = sent;
        }

        next += period;
        if (next < sent) next = sent;
        wait(next);
    }

    // Leave the panel dark rather than frozen on the last frame.
    if (panelOn) {
        u8g2_ClearBuffer(u8g2);
        u8g2_SendBuffer(u8g2);
        u8g2_SetPowerSave(u8g2, 1);
    }
}

}  // namespace bela
}  // namespace wf
