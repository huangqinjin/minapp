#ifndef MINAPP_SPINLOCK_HPP
#define MINAPP_SPINLOCK_HPP

#include <atomic>

namespace minapp
{
    class spinlock
    {
#if defined(__cpp_lib_atomic_value_initialization)
        std::atomic_flag guard_;
#else
        std::atomic_flag guard_ = ATOMIC_FLAG_INIT;
#endif

    public:
        spinlock() noexcept = default;
        spinlock(const spinlock&) noexcept {}
        spinlock& operator=(const spinlock&) noexcept { return *this; }
        bool try_lock() noexcept { return !guard_.test_and_set(std::memory_order_acquire); }
        void unlock() noexcept
        {
            guard_.clear(std::memory_order_release);
#if defined(__cpp_lib_atomic_wait)
            guard_.notify_one();
#endif
        }
        void lock() noexcept
        {
            while (!try_lock())
            {
#if defined(__cpp_lib_atomic_wait)
                guard_.wait(true, std::memory_order_relaxed);
#elif defined(__cpp_lib_atomic_flag_test)
                while (guard_.test(std::memory_order_relaxed));
#endif
            }
        }
    };
}
#endif
