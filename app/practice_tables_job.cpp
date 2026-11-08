// SPDX-License-Identifier: GPL-3.0-or-later
#include "practice_tables_job.hpp"
#include "crash_thread.hpp"
#include "loop_wake.hpp"

#include <utility>

namespace etude::app {

PracticeTablesJob::~PracticeTablesJob() {
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
        held_ = false;
        waiting_.reset();
        abort_ = true; // a build in progress ends at its next check, not at its end
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void PracticeTablesJob::request(TablesRequest request) {
    {
        const std::lock_guard lock(mutex_);
        // A build for an older request runs on and its answer is kept (take() hands the newest
        // finished): a stepper held faster than a build lasts would otherwise abort every build
        // and apply nothing until the taps paused.
        waiting_ = std::move(request);
        if (!thread_.joinable()) {
            thread_ = spawn_thread([this] { run(); });
        }
    }
    wake_.notify_one();
}

std::optional<TablesResult> PracticeTablesJob::take() {
    const std::lock_guard lock(mutex_);
    std::optional<TablesResult> out;
    out.swap(done_);
    return out;
}

void PracticeTablesJob::cancel() {
    {
        const std::lock_guard lock(mutex_);
        waiting_.reset();
        if (done_) { // freed by the worker: a heap vector for each note is milliseconds
            flow_garbage_.push_back(std::move(done_->flow));
            wait_garbage_.push_back(std::move(done_->wait));
            done_.reset();
        }
        ++epoch_;
        abort_ = true;
        if (!thread_.joinable() && (!flow_garbage_.empty() || !wait_garbage_.empty())) {
            thread_ = spawn_thread([this] { run(); });
        }
    }
    wake_.notify_one();
}

bool PracticeTablesJob::busy() const {
    const std::lock_guard lock(mutex_);
    return waiting_.has_value() || running_;
}

void PracticeTablesJob::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return !waiting_.has_value() && !running_; });
}

void PracticeTablesJob::hold(bool on) {
    {
        const std::lock_guard lock(mutex_);
        held_ = on;
    }
    wake_.notify_all();
}

void PracticeTablesJob::set_check_hook(std::function<void()> hook) {
    const std::lock_guard lock(mutex_);
    check_hook_ = std::move(hook);
}

void PracticeTablesJob::discard(FlowEvaluator::Prepared&& flow, WaitEvaluator::Prepared&& wait) {
    {
        const std::lock_guard lock(mutex_);
        flow_garbage_.push_back(std::move(flow));
        wait_garbage_.push_back(std::move(wait));
        if (!thread_.joinable()) {
            thread_ = spawn_thread([this] { run(); });
        }
    }
    wake_.notify_one();
}

void PracticeTablesJob::discard(std::vector<FlowEvaluator::Prepared>&& flow,
                                std::vector<WaitEvaluator::Prepared>&& wait) {
    {
        const std::lock_guard lock(mutex_);
        for (FlowEvaluator::Prepared& p : flow) {
            flow_garbage_.push_back(std::move(p));
        }
        for (WaitEvaluator::Prepared& p : wait) {
            wait_garbage_.push_back(std::move(p));
        }
        if (!thread_.joinable()) {
            thread_ = spawn_thread([this] { run(); });
        }
    }
    wake_.notify_one();
}

void PracticeTablesJob::run() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] {
            return stop_ || !flow_garbage_.empty() || !wait_garbage_.empty() ||
                   (waiting_.has_value() && !held_);
        });
        if (!flow_garbage_.empty() || !wait_garbage_.empty()) {
            std::vector<FlowEvaluator::Prepared> flow = std::move(flow_garbage_);
            std::vector<WaitEvaluator::Prepared> wait = std::move(wait_garbage_);
            flow_garbage_.clear();
            wait_garbage_.clear();
            running_ = true;
            lock.unlock();
            flow.clear(); // the frees
            wait.clear();
            lock.lock();
            running_ = false;
            if (!waiting_.has_value()) {
                idle_.notify_all();
            }
            continue; // also on stop: everything handed over is freed here before the thread ends
        }
        if (stop_) {
            return;
        }
        TablesRequest request = std::move(*waiting_);
        waiting_.reset();
        running_ = true;
        abort_ = false;
        const std::uint64_t epoch = epoch_;
        const std::function<void()> hook = check_hook_;
        lock.unlock();

        const FlowEvaluator::AbortCheck abort = [this, &hook] {
            if (hook) {
                hook();
            }
            return abort_.load();
        };
        TablesResult result;
        result.serial = request.serial;
        result.hands = request.hands;
        result.transposition = request.transposition;
        result.flow = FlowEvaluator::prepare(request.notes, request.measures, request.hands,
                                             request.transposition, request.fingerprint, abort);
        const bool whole = !abort_.load();
        if (whole) {
            result.wait = WaitEvaluator::prepare(request.notes, request.hands, request.transposition,
                                                 request.fingerprint, abort);
        }

        lock.lock();
        running_ = false;
        if (!whole) {
            ++stopped_builds_;
        }
        if (whole && !abort_.load() && epoch == epoch_ && !stop_) {
            done_ = std::move(result);
            wake_loop(WakeSource::PracticeTables);
        } else {
            flow_garbage_.push_back(std::move(result.flow)); // freed next round, here
            wait_garbage_.push_back(std::move(result.wait));
        }
        if (!waiting_.has_value() && flow_garbage_.empty() && wait_garbage_.empty()) {
            idle_.notify_all();
        }
    }
}

} // namespace etude::app
