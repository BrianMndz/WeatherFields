#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <JSON.h>
#include <libraries/Gui/Gui.h>

#include "BelaHost.h"
#include "wf/ControlOutbox.h"

namespace wf {
namespace bela {

// The browser panel's link to the rig, over Bela's Gui library: a WebSocket
// at ws://bela.local:5555/gui, and the project's sketch.js served at
// http://bela.local/gui. It speaks parameters by slot and ID, so it knows no
// rig; each app's sketch.js decides what to show.
//
// Messages from the page, as JSON objects:
//   {"event": "wf-hello", "id": <number>}                  answered with wf-state
//   {"event": "wf-param", "slot": <n>, "id": <n>, "value": <number>}
//   {"event": "wf-trigger", "slot": <n>, "id": <n>}
// Sent to the page:
//   {"event": "wf-state", "id": <the hello's id>, "rig": <name>,
//    "params": [[slot, id, value], ...]}                   every parameter but triggers
//   {"event": "wf-update", "input": <level>, "output": <level>,
//    "params": [[slot, id, value], ...]}                   about 30 times a second
// An update's params are those whose published value changed since the last
// update. Levels are linear peaks (1 is full scale). The library sends to
// every open page, so a page takes the wf-state whose id answers its own
// hello.
//
// Two threads produce events for render(): the library's WebSocket thread,
// as messages arrive, and the sender thread, which retries what waited
// (wf::ControlOutbox's rules). One mutex makes them a single producer. Only
// the sender thread sends to the page: gui.sendControl() is not real-time
// safe, and one sender keeps the messages in order.
//
// A GuiLink lives until the process exits; the host never destroys it. Bela's
// Gui cannot stop its WebSocket server synchronously (WSServer::cleanup()
// only asks seasocks to terminate, and nothing joins its threads), so a
// message can arrive during or after cleanup(). Stop() closes the link under
// the mutex; a message that comes later returns before touching anything
// else, and the mutex it needs is never destroyed.
class GuiLink {
public:
    GuiLink(const Rig& rig, Published& published, GuiInput& input);

    GuiLink(const GuiLink&) = delete;
    GuiLink& operator=(const GuiLink&) = delete;

    // From setup(), after the rig is prepared and published.
    void Start(const char* projectName);

    // From cleanup(): later messages are ignored and the sender thread ends.
    void Stop();

private:
    bool OnMessage(JSONObject& root);
    void Run();
    void SendState(double id);
    void SendUpdate();

    const Rig& rig_;
    Published& published_;
    Gui gui_;

    std::mutex mutex_;                    // guards open_, outbox_ and hellos_
    bool open_ = false;                   // between Start() and Stop()
    ControlOutbox<kGuiInputCapacity> outbox_;
    std::vector<double> hellos_;          // ids of hellos to answer

    std::vector<float> sent_;             // sender thread: values in the last update
    bool firstUpdate_ = true;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace bela
}  // namespace wf
