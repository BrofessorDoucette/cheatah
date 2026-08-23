// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "thread.hpp"

#include <iostream>
#include <stdexcept>

namespace cheatah::thread {

Thread::Thread(std::thread t, std::shared_ptr<detail::State> state) noexcept
    : t_(std::move(t)), state_(std::move(state)) {}

Thread::Thread(Thread&& other) noexcept = default;

Thread& Thread::operator=(Thread&& other) noexcept {
    if (this != &other) {
        settle();  // the old thread is joined (and an unobserved error reported) before adopting
        t_ = std::move(other.t_);
        state_ = std::move(other.state_);
    }
    return *this;
}

Thread::~Thread() { settle(); }

void Thread::join() {
    if (!t_.joinable()) {
        throw std::runtime_error("thread.join: nothing to join (already joined or moved away)");
    }
    t_.join();
    if (state_ && state_->error) {
        state_->observed = true;
        std::rethrow_exception(state_->error);
    }
}

bool Thread::joinable() const noexcept { return t_.joinable(); }

void Thread::settle() noexcept {
    if (t_.joinable()) t_.join();
    if (state_ && state_->error && !state_->observed) {
        state_->observed = true;
        // A destructor must not throw, so an exception nobody joined for is REPORTED, not lost —
        // the analogue of Python's default excepthook for a thread.
        std::string what;
        try {
            std::rethrow_exception(state_->error);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
            what = "unknown error";  // a non-std::exception payload carries no message
        }
        std::cerr << "cheatah thread: unhandled exception in thread: " << what << "\n";
    }
}

}  // namespace cheatah::thread
