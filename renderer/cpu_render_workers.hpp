#pragma once
// Small persistent CPU worker pool for renderer-only, immutable work.
// No OpenGL context, engine pointers, COG or physics calls are permitted
// inside a dispatched job. The submitting render thread is the third lane.
#include <array>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace jkgm {
class cpu_render_workers {
public:
    static constexpr unsigned background_workers=2U;
    static constexpr unsigned lanes=background_workers+1U;

    // Cheap greedy assignment, without sorting or shared atomic work queues.
    // The caller estimates work; this helper does not access engine state.
    static unsigned weighted_lane(std::array<std::size_t,lanes> &weights,
                                  std::size_t cost) {
        unsigned lane=0U;
        for(unsigned i=1U;i<lanes;++i)
            if(weights[i]<weights[lane])lane=i;
        weights[lane]+=cost;
        return lane;
    }

    cpu_render_workers() {
        try {
            for(unsigned i=0;i<background_workers;++i)
                threads_[i]=std::thread([this,i] { worker_loop(i); });
            available_=true;
        } catch(...) {
            shutdown();
        }
    }
    cpu_render_workers(cpu_render_workers const &)=delete;
    cpu_render_workers &operator=(cpu_render_workers const &)=delete;
    ~cpu_render_workers() { shutdown(); }

    bool available() const { return available_; }

    // Synchronous barrier: all worker reads and writes finish before the
    // render thread processes the prepared results. Failure is reported so
    // callers can discard partial outputs and use the serial path.
    template<class Fn>
    bool execute(Fn &&fn) {
        if(!available_)return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            failure_=nullptr;
            complete_=0U;
            task_=std::forward<Fn>(fn);
            ++epoch_;
        }
        wake_.notify_all();
        std::exception_ptr local_failure;
        try {
            task_(background_workers);
        } catch(...) {
            local_failure=std::current_exception();
        }
        {
            std::unique_lock<std::mutex> lock(mutex_);
            finished_.wait(lock,[this] {
                return complete_==background_workers;
            });
            // Avoid dangling references after the caller returns.
            task_={};
            if(failure_ || local_failure)return false;
        }
        return true;
    }

private:
    std::thread threads_[background_workers]{};
    std::mutex mutex_;
    std::condition_variable wake_,finished_;
    std::function<void(unsigned)> task_;
    std::exception_ptr failure_;
    uint64_t epoch_=0;
    unsigned complete_=0;
    bool stop_=false,available_=false;

    void worker_loop(unsigned lane) {
        std::unique_lock<std::mutex> lock(mutex_);
        uint64_t seen=0;
        for(;;) {
            wake_.wait(lock,[this,&seen] {
                return stop_ || epoch_!=seen;
            });
            if(stop_)return;
            seen=epoch_;
            lock.unlock();
            std::exception_ptr failure;
            try {
                task_(lane);
            } catch(...) {
                failure=std::current_exception();
            }
            lock.lock();
            if(failure && !failure_)failure_=failure;
            if(++complete_==background_workers)
                finished_.notify_one();
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_=true;
        }
        wake_.notify_all();
        for(auto &thread:threads_)
            if(thread.joinable())thread.join();
        available_=false;
    }
};
} // namespace jkgm
