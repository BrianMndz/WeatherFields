#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace wf {

// A fixed-capacity, lock-free queue between exactly one producer thread and
// exactly one consumer thread, such as the MIDI thread and the audio thread.
// Neither side ever waits, allocates or makes a system call, so the consumer
// may be a real-time audio thread. Items keep their order and none is lost
// unless TryPush() reports the queue full.
//
// The indices count every item ever pushed or popped and wrap modulo 2^n;
// a power-of-two capacity divides 2^n, so `index & kMask` stays the same slot
// across the wrap and all Capacity slots are usable.
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value,
                  "items are copied in and out of the slots");

    // Bela's libstdc++ 6 lacks is_always_lock_free; this builtin answers the
    // same question at compile time.
#if defined(__GNUC__)
    static_assert(__atomic_always_lock_free(sizeof(std::size_t), 0),
                  "the indices must be lock-free atomics");
#endif

public:
    static constexpr std::size_t kCapacity = Capacity;

    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer thread only. Returns false, dropping the item, when full.
    bool TryPush(const T& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail - head == Capacity) return false;
        slots_[tail & kMask] = item;
        // Release: the consumer that sees the new tail also sees the slot.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only. Returns false, leaving `item` alone, when empty.
    bool TryPop(T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) return false;
        item = slots_[head & kMask];
        // Release: the producer reuses the slot only after this read.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // Padding keeps the two indices on separate cache lines, so the two
    // threads do not invalidate each other's line on every operation.
    // alignas(64) would say the same, but C++14's new does not honour
    // alignment above alignof(std::max_align_t) for heap-allocated queues.
    static constexpr std::size_t kCacheLine = 64;

    std::atomic<std::size_t> head_{0};  // written by the consumer
    char headPadding_[kCacheLine - sizeof(std::atomic<std::size_t>)];
    std::atomic<std::size_t> tail_{0};  // written by the producer
    char tailPadding_[kCacheLine - sizeof(std::atomic<std::size_t>)];
    T slots_[Capacity];
};

}  // namespace wf
