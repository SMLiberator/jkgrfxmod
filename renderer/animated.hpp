#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace jkgm {
    // Seconds on a monotonic clock, counted from the first call.
    //
    // This is wall-clock time, so a fade keeps running while the game is paused or sitting in
    // a menu. Fades are evaluated when the value is read (at draw time), so nothing has to
    // tick them every frame.
    inline double effect_clock_now()
    {
        static auto const start = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    // A group of N floats that can be set instantly or faded linearly to a new value.
    template <size_t N>
    class animated_floats {
    public:
        using values = std::array<float, N>;

        animated_floats() = default;

        explicit animated_floats(values const &initial)
            : from_(initial)
            , to_(initial)
        {
        }

        // The value at time `now`: partway between the old and new value while fading.
        values value(double now = effect_clock_now()) const
        {
            if(duration_ <= 0.0 || now >= start_time_ + duration_) {
                return to_;
            }

            if(now <= start_time_) {
                return from_;
            }

            float const t = static_cast<float>((now - start_time_) / duration_);

            values result{};
            for(size_t i = 0; i < N; ++i) {
                result[i] = from_[i] + (to_[i] - from_[i]) * t;
            }

            return result;
        }

        // The value this will settle on once any fade has finished.
        values target() const
        {
            return to_;
        }

        // Jump straight to a value, cancelling any fade.
        void set(values const &v)
        {
            from_ = v;
            to_ = v;
            duration_ = 0.0;
        }

        // Fade from whatever is showing right now to `target` over `seconds`. A fade that
        // starts in the middle of another one begins from the current blended value, so
        // there is no visible jump. Zero, negative or invalid durations set the value at once.
        void fade_to(values const &target, float seconds, double now = effect_clock_now())
        {
            if(!(seconds > 0.0f)) {
                set(target);
                return;
            }

            from_ = value(now);
            to_ = target;
            start_time_ = now;
            duration_ = static_cast<double>(seconds);
        }

        bool is_fading(double now = effect_clock_now()) const
        {
            return duration_ > 0.0 && now < start_time_ + duration_;
        }

    private:
        values from_{};
        values to_{};
        double start_time_ = 0.0;
        double duration_ = 0.0;
    };
}
