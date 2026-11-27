#pragma once
// Small persistent CPU worker pool for renderer-only, immutable work.
// No OpenGL context, engine pointers, COG or physics calls are permitted
// inside a dispatched job. The submitting render thread is the final lane.
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
    static constexpr unsigned lanes=background_workers+1U; // default layout
    static constexpr unsigned max_background_workers=5U;
    static constexpr unsigned max_lanes=max_background_workers+1U;

    // Cheap greedy assignment, without sorting or shared atomic work queues.
    // The caller estimates work; this helper does not access engine state.
    template<std::size_t N>
    static unsigned weighted_lane(std::array<std::size_t,N> &weights,
                                  std::size_t cost,unsigned active_lanes=unsigned(N)) {
        unsigned lane=0U;
        for(unsigned i=1U;i<active_lanes && i<N;++i)
            if(weights[i]<weights[lane])lane=i;
        weights[lane]+=cost;
        return lane;
    }

    explicit cpu_render_workers(unsigned count=background_workers)
        :worker_count_(count<1U?1U:count>max_background_workers?max_background_workers:count) {
        try {
            for(unsigned i=0;i<worker_count_;++i)
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
    unsigned lane_count() const { return worker_count_+1U; }

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
            task_(worker_count_);
        } catch(...) {
            local_failure=std::current_exception();
        }
        {
            std::unique_lock<std::mutex> lock(mutex_);
            finished_.wait(lock,[this] {
                return complete_==worker_count_;
            });
            // Avoid dangling references after the caller returns.
            task_={};
            if(failure_ || local_failure)return false;
        }
        return true;
    }

private:
    unsigned const worker_count_;
    std::thread threads_[max_background_workers]{};
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
            if(++complete_==worker_count_)
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
