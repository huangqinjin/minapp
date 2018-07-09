#ifndef MINAPP_SPINLOCK_HPP
#define MINAPP_SPINLOCK_HPP

#include <atomic>

namespace minapp
{
    class spinlock
    {
        std::atomic_flag guard_ = ATOMIC_FLAG_INIT;

    public:
        spinlock() = default;
        spinlock(const spinlock&) {}
        spinlock& operator=(const spinlock&) { return *this; }
        bool try_lock() { return !guard_.test_and_set(std::memory_order_acquire); }
        void lock() { while(!try_lock()) continue; }
        void unlock() { guard_.clear(std::memory_order_release); }
    };
}
#endif
