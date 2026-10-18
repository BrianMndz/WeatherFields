#include "GuiLink.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

// Included here as well as in the header: Bela's Makefile picks the
// libraries to link from the project's own source files.
#include <libraries/Gui/Gui.h>

#include "wf/Dsp.h"

namespace wf {
namespace bela {

namespace {

// The JUCE editor's LED refresh rate. Updates are sent even when nothing
// changed, so a page notices when they stop.
constexpr auto kUpdateInterval = std::chrono::milliseconds(33);

// Hellos waiting for an answer; more are dropped (a page asks again when its
// connection opens).
constexpr std::size_t kMaxHellos = 16;

const JSONValue* Find(const JSONObject& object, const wchar_t* key) {
    const auto found = object.find(key);
    return found == object.end() ? nullptr : found->second;
}

// A whole number from 0 to `maximum` in `key`.
bool FindIndex(const JSONObject& object, const wchar_t* key, double maximum, unsigned int& result) {
    const JSONValue* value = Find(object, key);
    if (value == nullptr || !value->IsNumber()) return false;
    const double number = value->AsNumber();
    if (!(number >= 0.0 && number <= maximum && number == std::floor(number))) return false;
    result = static_cast<unsigned int>(number);
    return true;
}

JSONValue* ParamEntry(unsigned int slot, unsigned int id, float value) {
    JSONArray entry;
    entry.push_back(new JSONValue(static_cast<int>(slot)));
    entry.push_back(new JSONValue(static_cast<int>(id)));
    entry.push_back(new JSONValue(static_cast<double>(value)));
    return new JSONValue(entry);
}

}  // namespace

GuiLink::GuiLink(const Rig& rig, Published& published, GuiInput& input)
    : rig_(rig), published_(published), outbox_(input) {}

void GuiLink::Start(const char* projectName) {
    sent_.assign(published_.params.size(), 0.0f);
    firstUpdate_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = true;
    }
    // The callback is set before the server starts, so no message arrives
    // without it. Like Bela's examples, setup()'s result is not checked.
    gui_.setControlDataCallback([this](JSONObject& root, void*) { return OnMessage(root); });
    gui_.setup(projectName);
    running_.store(true);
    thread_ = std::thread([this] { Run(); });
}

void GuiLink::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
    }
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

// On the library's WebSocket thread. Invalid messages are ignored; the rig
// validates values again. Only `root` is read before the link is known to be
// open; the rig's state, which the host's statics own, only after, under the
// mutex. Returns true so the library's own handling, which tracks the
// connection, still sees every message.
bool GuiLink::OnMessage(JSONObject& root) {
    const JSONValue* event = Find(root, L"event");
    if (event == nullptr || !event->IsString()) return true;
    const std::wstring& name = event->AsString();

    if (name == L"wf-hello") {
        const JSONValue* id = Find(root, L"id");
        if (id != nullptr && id->IsNumber()) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (open_ && hellos_.size() < kMaxHellos) hellos_.push_back(id->AsNumber());
        }
        return true;
    }

    if (name == L"wf-focus") return OnFocus(root);

    const bool trigger = name == L"wf-trigger";
    if (!trigger && name != L"wf-param") return true;

    unsigned int slot = 0;
    unsigned int id = 0;
    if (!FindIndex(root, L"slot", 255.0, slot) || !FindIndex(root, L"id", 65535.0, id)) return true;
    float value = 1.0f;
    if (!trigger) {
        const JSONValue* number = Find(root, L"value");
        if (number == nullptr || !number->IsNumber()) return true;
        value = static_cast<float>(number->AsNumber());
        if (!IsFinite(value)) return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return true;
    const ParamSpec* spec = nullptr;
    for (const Published::Param& param : published_.params) {
        if (param.slot == slot && param.spec->id == id) {
            spec = param.spec;
            break;
        }
    }
    if (spec == nullptr) return true;
    outbox_.Send(ControlEvent{trigger ? ControlEvent::Type::Trigger : ControlEvent::Type::Param,
                              static_cast<std::uint8_t>(slot), static_cast<ParamId>(id), value},
                 spec->kind);
    return true;
}

// On the library's WebSocket thread, as OnMessage(). A message naming no
// page, or setting a parameter the rig lacks, is ignored.
bool GuiLink::OnFocus(const JSONObject& root) {
    const JSONValue* page = Find(root, L"page");
    if (page == nullptr || !page->IsNumber()) return true;
    const bool clear = Find(root, L"slot") == nullptr && Find(root, L"id") == nullptr;
    unsigned int slot = 0;
    unsigned int id = 0;
    if (!clear && (!FindIndex(root, L"slot", 255.0, slot) || !FindIndex(root, L"id", 65535.0, id))) return true;
    const JSONValue* repeat = Find(root, L"repeat");
    const bool repeated = repeat != nullptr && repeat->IsBool() && repeat->AsBool();

    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return true;
    const std::uint32_t now = FocusClockMs();
    const bool held = published_.focus.load(std::memory_order_relaxed) >= 0 &&
                      now - published_.focusMs.load(std::memory_order_relaxed) <= kFocusLifetimeMs;
    const bool owner = held && page->AsNumber() == focusOwner_;
    if (clear) {
        if (owner) published_.focus.store(-1, std::memory_order_relaxed);
        return true;
    }
    if (repeated && held && !owner) return true;  // another page's focus stands

    for (std::size_t i = 0; i < published_.params.size(); ++i) {
        if (published_.params[i].slot == slot && published_.params[i].spec->id == id) {
            focusOwner_ = page->AsNumber();
            published_.focus.store(static_cast<std::int32_t>(i), std::memory_order_relaxed);
            published_.focusMs.store(now, std::memory_order_relaxed);
            break;
        }
    }
    return true;
}

// The sender thread: an ordinary Linux thread.
void GuiLink::Run() {
    std::vector<double> hellos;
    while (running_.load()) {
        std::this_thread::sleep_for(kUpdateInterval);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            outbox_.Flush();
            hellos.swap(hellos_);
        }
        for (const double id : hellos) SendState(id);
        hellos.clear();
        SendUpdate();
    }
}

void GuiLink::SendState(double id) {
    JSONObject root;
    root[L"event"] = new JSONValue(L"wf-state");
    root[L"id"] = new JSONValue(id);
    const char* name = rig_.Name();
    root[L"rig"] = new JSONValue(std::wstring(name, name + std::strlen(name)));

    JSONArray params;
    for (std::size_t i = 0; i < published_.params.size(); ++i) {
        const Published::Param& param = published_.params[i];
        if (param.spec->kind == ParamKind::Trigger) continue;
        params.push_back(ParamEntry(param.slot, param.spec->id, published_.values[i].load(std::memory_order_relaxed)));
    }
    root[L"params"] = new JSONValue(params);

    // The value owns, and deletes, everything stored in root.
    JSONValue message(root);
    gui_.sendControl(&message);
}

void GuiLink::SendUpdate() {
    JSONObject root;
    root[L"event"] = new JSONValue(L"wf-update");
    root[L"input"] = new JSONValue(static_cast<double>(published_.inputLevel.load(std::memory_order_relaxed)));
    root[L"output"] = new JSONValue(static_cast<double>(published_.outputLevel.load(std::memory_order_relaxed)));

    JSONArray params;
    for (std::size_t i = 0; i < published_.params.size(); ++i) {
        const Published::Param& param = published_.params[i];
        if (param.spec->kind == ParamKind::Trigger) continue;
        const float value = published_.values[i].load(std::memory_order_relaxed);
        if (!firstUpdate_ && value == sent_[i]) continue;
        sent_[i] = value;
        params.push_back(ParamEntry(param.slot, param.spec->id, value));
    }
    firstUpdate_ = false;
    root[L"params"] = new JSONValue(params);

    JSONValue message(root);
    gui_.sendControl(&message);
}

}  // namespace bela
}  // namespace wf
