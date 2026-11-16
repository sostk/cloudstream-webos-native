// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime/runtime_client.hpp"

#include "wnk/log.hpp"

#include <cstdio>

#include <sys/wait.h>

namespace csweb::runtime {

namespace {
constexpr const char *kCat = "csweb";
constexpr int kMaxRestarts = 3;
}  // namespace

RuntimeClient::RuntimeClient(SpecFactory make_spec, Post post) : make_spec_(std::move(make_spec)), post_(std::move(post)) {}

RuntimeClient::~RuntimeClient() {
    *alive_ = false;
    open_.clear();  // the owners of these handlers are being torn down too: do not call them
    on_state = nullptr;
    stop();
}

void RuntimeClient::set_state(State s) {
    if (state_ == s) return;
    state_ = s;
    if (on_state) on_state(s);
}

void RuntimeClient::start() {
    if (state_ == State::Starting || state_ == State::Ready) return;
    restarts_ = 0;
    launch();
}

void RuntimeClient::stop() {
    ++generation_;
    if (child_) {
        child_->close_stdin();  // end of input is a shutdown request
        child_->kill();
        child_.reset();         // joins the reader
    }
    for (auto &kv : open_) {
        if (kv.second.on_done) kv.second.on_done(false, "runtime stopped");
    }
    open_.clear();
    queued_.clear();
    set_state(State::Stopped);
}

void RuntimeClient::launch() {
    core::ChildSpec spec;
    std::string error;
    if (!make_spec_ || !make_spec_(&spec, &error)) {
        last_error_ = error;
        WNK_LOGW(kCat, "runtime: cannot start: %s", error.c_str());
        set_state(State::Failed);
        return;
    }
    // Keep the previous run's diagnostics for one restart; the log lives on tmpfs (RAM).
    if (!spec.stderr_path.empty()) std::rename(spec.stderr_path.c_str(), (spec.stderr_path + ".1").c_str());
    child_ = std::make_unique<core::ChildProcess>();
    const uint64_t gen = ++generation_;
    std::weak_ptr<bool> alive = alive_;
    set_state(State::Starting);
    const bool started = child_->start(
        spec,
        [this, gen, alive](std::string line) {
            post_([this, gen, alive, line = std::move(line)] {
                if (alive.expired() || gen != generation_) return;
                handle_line(line);
            });
        },
        [this, gen, alive](int status) {
            post_([this, gen, alive, status] {
                if (alive.expired() || gen != generation_) return;
                handle_exit(status);
            });
        },
        &error);
    if (!started) {
        last_error_ = error;
        WNK_LOGW(kCat, "runtime: start failed: %s", error.c_str());
        child_.reset();
        set_state(State::Failed);
    }
}

void RuntimeClient::send(const nlohmann::json &j) {
    const std::string line = j.dump();
    if (state_ == State::Ready && child_ && child_->write_line(line)) return;
    queued_.push_back(line);
}

RuntimeClient::RequestId RuntimeClient::request(nlohmann::json req, EventHandler on_event, DoneHandler on_done) {
    if (state_ == State::Failed) {
        if (on_done) on_done(false, last_error_.empty() ? "plugin runtime unavailable" : last_error_);
        return 0;
    }
    const RequestId id = next_id_++;
    const std::string key = "r" + std::to_string(id);
    req["id"] = key;
    open_[key] = Open{std::move(on_event), std::move(on_done), {}};
    send(req);
    return id;
}

void RuntimeClient::cancel(RequestId id) {
    const std::string key = "r" + std::to_string(id);
    if (!open_.count(key)) return;
    send({{"id", "c" + std::to_string(next_id_++)}, {"op", "cancel"}, {"target", key}});
}

void RuntimeClient::answer_dialog(int dialog, nlohmann::json answer) {
    answer["op"] = "answer";
    answer["dialog"] = dialog;
    send(answer);
}

void RuntimeClient::handle_line(const std::string &line) {
    auto j = nlohmann::json::parse(line, nullptr, false);
    if (!j.is_object() || !j.contains("event")) return;  // not protocol output
    const std::string event = j.value("event", "");
    if (event == "ready") {
        WNK_LOGI(kCat, "runtime: ready");
        restarts_ = 0;
        set_state(State::Ready);
        auto pending = std::move(queued_);
        queued_.clear();
        for (const auto &l : pending) {
            if (!child_ || !child_->write_line(l)) queued_.push_back(l);
        }
        if (on_ready) on_ready();
        return;
    }
    if ((event == "dialog" || event == "dialog_closed" || event == "toast" || event == "open_url") && on_dialog) on_dialog(j);
    const std::string id = j.value("id", "");
    auto it = open_.find(id);
    if (it == open_.end()) {
        if (on_unrouted) on_unrouted(j);
        return;
    }
    if (event == "error") {
        it->second.last_error = j.value("message", j.value("type", std::string("error")));
    }
    if (event == "done") {
        Open done = std::move(it->second);
        open_.erase(it);
        const bool ok = j.value("ok", false);
        if (done.on_done) done.on_done(ok, ok ? std::string() : (done.last_error.empty() ? "failed" : done.last_error));
        return;
    }
    if (it->second.on_event) it->second.on_event(j);
}

void RuntimeClient::handle_exit(int status) {
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    WNK_LOGW(kCat, "runtime: exited (code %d, signal %d)", code, sig);
    child_.reset();
    auto open = std::move(open_);
    open_.clear();
    for (auto &kv : open) {
        if (kv.second.on_done) kv.second.on_done(false, "plugin runtime stopped");
    }
    if (++restarts_ > kMaxRestarts) {
        last_error_ = "plugin runtime keeps stopping (see runtime.log)";
        set_state(State::Failed);
        return;
    }
    launch();
}

}  // namespace csweb::runtime
