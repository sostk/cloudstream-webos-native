// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's side of the plugin runtime service (docs/runtime/service-protocol.md): one long-lived ART
// process, JSON requests on its stdin, JSON-lines events on its stdout. Each request gets an id; its
// events and its single `done` come back to the handlers given with it.
//
// Threading: every public method and every handler runs on the main thread. Output lines arrive on
// the child's reader thread and are handed over through `post` (the kit's MainDispatcher).
//
// Failure: if the process exits, every open request ends with ok=false ("runtime stopped") and the
// client restarts the process after a short backoff (3 attempts in a row at most); `on_ready` then
// fires again so the owner can reload plugins, exactly as after the first start.
#pragma once

#include "core/child_process.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace csweb::runtime {

// The error text of a request the app cancelled (Service.cancel), for callers that treat it as no error.
inline constexpr const char *kCancelledByApp = "cancelled by the app";

class RuntimeClient {
 public:
    enum class State { Stopped, Starting, Ready, Failed };
    using Post = std::function<void(std::function<void()>)>;
    using SpecFactory = std::function<bool(core::ChildSpec *spec, std::string *error)>;
    using EventHandler = std::function<void(const nlohmann::json &event)>;
    using DoneHandler = std::function<void(bool ok, const std::string &error)>;
    using RequestId = uint64_t;

    RuntimeClient(SpecFactory make_spec, Post post);
    ~RuntimeClient();

    void start();
    void stop();
    State state() const { return state_; }
    const std::string &last_error() const { return last_error_; }

    // Sends `request` (an object; "id" is set here). Requests made before the service is ready are
    // queued. Returns the id (0 if the runtime has failed for good).
    RequestId request(nlohmann::json request, EventHandler on_event, DoneHandler on_done);
    void cancel(RequestId id);
    // Answers dialog `dialog` (fields/click/item/button, dialog-protocol.md). Never logged.
    void answer_dialog(int dialog, nlohmann::json answer);

    std::function<void()> on_ready;                              // the service printed "ready"
    std::function<void(State)> on_state;                         // every state change
    // dialog, dialog_closed and toast events of every request (the app shows plugin UI wherever it
    // comes from); they still reach the request's own handler too.
    std::function<void(const nlohmann::json &)> on_dialog;
    std::function<void(const nlohmann::json &)> on_unrouted;     // events without a known id

    // For tests: how many requests are open.
    size_t open_requests() const { return open_.size(); }

 private:
    struct Open {
        EventHandler on_event;
        DoneHandler on_done;
        std::string last_error;
    };
    void launch();
    void handle_line(const std::string &line);
    void handle_exit(int status);
    void set_state(State s);
    void send(const nlohmann::json &j);

    SpecFactory make_spec_;
    Post post_;
    std::unique_ptr<core::ChildProcess> child_;
    State state_ = State::Stopped;
    std::string last_error_;
    RequestId next_id_ = 1;
    std::map<std::string, Open> open_;
    std::vector<std::string> queued_;  // lines waiting for "ready"
    int restarts_ = 0;
    uint64_t generation_ = 0;          // ignores lines and exits of a replaced child
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace csweb::runtime
