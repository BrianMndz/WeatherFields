// A minimal Bela program in the style Render.cpp uses. It must compile; if it
// does not, the toolchain or headers are misconfigured rather than the project.
#include <Bela.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace {
alignas(4) unsigned char arena[1024];
std::array<float, 16> scratch{};
std::atomic<float> gain{0.5f};
}  // namespace

bool setup(BelaContext* context, void*)
{
    return context->audioInChannels == 2 && gain.is_lock_free() && arena[0] == 0;
}

void render(BelaContext* context, void*)
{
    for (unsigned int n = 0; n < context->audioFrames && n < scratch.size(); ++n) {
        scratch[n] = audioRead(context, n, 0) * gain.load(std::memory_order_relaxed);
        audioWrite(context, n, 0, scratch[n]);
    }
}

void cleanup(BelaContext*, void*)
{
}
