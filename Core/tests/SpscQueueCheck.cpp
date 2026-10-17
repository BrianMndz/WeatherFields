#include <cstdint>
#include <iostream>
#include <thread>

#include "wf/Events.h"
#include "wf/SpscQueue.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void CheckSingleThread() {
    wf::SpscQueue<int, 4> queue;
    int item = -1;

    Expect(!queue.TryPop(item) && item == -1, "an empty queue pops nothing and leaves the item");
    for (int i = 0; i < 4; ++i) {
        Expect(queue.TryPush(i), "pushes up to the capacity succeed");
    }
    Expect(!queue.TryPush(4), "a push into a full queue fails");
    for (int i = 0; i < 4; ++i) {
        Expect(queue.TryPop(item) && item == i, "items come out in order");
    }
    Expect(!queue.TryPop(item), "the queue is empty again");

    // Many laps around the four slots, at every fill level from 1 to 3.
    int next = 100;
    int expected = 100;
    for (int lap = 0; lap < 1000; ++lap) {
        const int count = 1 + lap % 3;
        for (int i = 0; i < count; ++i) {
            Expect(queue.TryPush(next++), "pushes succeed while there is room");
        }
        for (int i = 0; i < count; ++i) {
            if (!queue.TryPop(item) || item != expected++) {
                Expect(false, "items keep their order across the wrap");
                return;
            }
        }
    }
}

// The use the queue exists for: one thread producing events while another
// drains them, here as fast as both can go with a small queue so that it is
// often full and often empty. Every event must arrive, once, in order.
void CheckTwoThreads() {
    constexpr std::uint32_t kEvents = 1000000;
    wf::SpscQueue<wf::ControlEvent, 64> queue;

    std::thread producer([&queue] {
        for (std::uint32_t i = 0; i < kEvents; ++i) {
            const wf::ControlEvent event{
                wf::ControlEvent::Type::Param,
                static_cast<std::uint8_t>(i & 0xffu),
                static_cast<wf::ParamId>(i >> 8),
                static_cast<float>(i & 0xffffu),
            };
            while (!queue.TryPush(event)) {
                std::this_thread::yield();
            }
        }
    });

    std::uint32_t received = 0;
    bool inOrder = true;
    wf::ControlEvent event{};
    while (received < kEvents) {
        if (!queue.TryPop(event)) {
            std::this_thread::yield();
            continue;
        }
        const std::uint32_t i = received++;
        if (event.type != wf::ControlEvent::Type::Param ||
            event.target != static_cast<std::uint8_t>(i & 0xffu) ||
            event.id != static_cast<wf::ParamId>(i >> 8) ||
            event.value != static_cast<float>(i & 0xffffu)) {
            inOrder = false;
        }
    }
    producer.join();

    Expect(inOrder, "every event arrives intact and in order across threads");
    Expect(!queue.TryPop(event), "nothing arrives after the last event");
}

}  // namespace

int main() {
    CheckSingleThread();
    CheckTwoThreads();

    if (failures != 0) {
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
