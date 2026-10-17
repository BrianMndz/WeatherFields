// expect-error: no member named 'is_always_lock_free'
// The JUCE host's static_assert uses this C++17 member; libstdc++ 6 lacks it.
#include <atomic>

static_assert(std::atomic<float>::is_always_lock_free, "");
