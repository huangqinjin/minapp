#ifndef MINAPP_PERSISTENT_BUFFER_MANAGER_HPP
#define MINAPP_PERSISTENT_BUFFER_MANAGER_HPP

#include "spinlock.hpp"
#include "persistent_buffer.hpp"

#include <mutex>

#include <boost/pool/pool.hpp>

namespace minapp
{
    class persistent_buffer_manager
    {
        boost::pool<> pool;
        std::pair<persistent_buffer*, persistent_buffer*> cached_buffers_pool;
        persistent_buffer_list cached_buffers_list;
        persistent_buffer_list managed_buffers_list;
        persistent_buffer_list marked_buffers_list;
        spinlock pool_guard;
        spinlock cached_buffers_list_guard;
        spinlock managed_buffers_list_guard;
        spinlock marked_buffers_list_guard;
        long marker;

    public:
        explicit persistent_buffer_manager(unsigned cached = 8)
            : pool(sizeof(persistent_buffer)), marker(0)
        {
            persistent_buffer* p = (persistent_buffer*) pool.ordered_malloc(cached);
            cached_buffers_pool.first = p;
            cached_buffers_pool.second = p + cached;
            while (p < cached_buffers_pool.second)
                cached_buffers_list.push_front(*::new((void*)p++) persistent_buffer);
        }

        ////////////////////////////////////////////////////////////////////
        /// manage() & mark() & marked() may be called in arbitrary threads,
        /// but marked() & clear_marked() should be protected by marker such
        /// that no concurrent call to them.
        ////////////////////////////////////////////////////////////////////
        void manage(persistent_buffer& b)
        {
            persistent_buffer& buf = hold_buffer(b);
            std::lock_guard<spinlock> guard(managed_buffers_list_guard);
            managed_buffers_list.push_back(buf);
        }

        void manage(persistent_buffer&& b)
        {
            return manage(b);
        }

        template<typename ...Buffers>
        std::enable_if_t<sizeof...(Buffers) >= 2> manage(Buffers&&... buffers)
        {
            persistent_buffer_list tmp = make_list(hold_buffer(buffers)...);
            std::lock_guard<spinlock> guard(managed_buffers_list_guard);
            managed_buffers_list.splice(managed_buffers_list.end(), tmp);  // Constant time
        }

        void manage(persistent_buffer_list& list)
        {
            persistent_buffer_list tmp;
            for (persistent_buffer& b : list)
                tmp.push_back(hold_buffer(b));

            std::lock_guard<spinlock> guard(managed_buffers_list_guard);
            managed_buffers_list.splice(managed_buffers_list.end(), tmp);  // Constant time
        }

        void manage(persistent_buffer_list&& list)
        {
            return manage(list);
        }

        long mark()
        {
            std::lock_guard<spinlock> guard(marked_buffers_list_guard);
            if (marked_buffers_list.empty())
            {
                std::lock_guard<spinlock> guard(managed_buffers_list_guard);
                if (!managed_buffers_list.empty())
                {
                    marked_buffers_list.swap(managed_buffers_list);
                    return marker == std::numeric_limits<long>::max() ? marker = 1 : ++marker;
                }
            }
            return -marker;
        }

        persistent_buffer_list& marked()
        {
            return marked_buffers_list;
        }

        void clear_marked()
        {
            marked_buffers_list.clear_and_dispose([this](persistent_buffer_list::pointer p)
            {
                free_buffer(p);
            });
        }

        ~persistent_buffer_manager()
        {
            auto deleter = [](persistent_buffer_list::pointer p)
            {
                p->~persistent_buffer();
            };

            cached_buffers_list.clear_and_dispose(deleter);
            managed_buffers_list.clear_and_dispose(deleter);
            marked_buffers_list.clear_and_dispose(deleter);
        }

    private:
        persistent_buffer& hold_buffer(persistent_buffer& b)
        {
            persistent_buffer* p = nullptr;

            {
                std::lock_guard<spinlock> guard(cached_buffers_list_guard);
                if (!cached_buffers_list.empty())
                {
                    p = &cached_buffers_list.front();
                    cached_buffers_list.pop_front();
                }
            }

            if (p == nullptr)
            {
                void* q = [this] {
                    std::lock_guard<spinlock> guard(pool_guard);
                    return pool.malloc();
                }();
                p = ::new(q) persistent_buffer{ b };
            }
            else
            {
                *p = b;
            }

            return *p;
        }

        void free_buffer(persistent_buffer* p)
        {
            if (p >= cached_buffers_pool.first && p < cached_buffers_pool.second)
            {
                p->storage() = {};
                std::lock_guard<spinlock> guard(cached_buffers_list_guard);
                cached_buffers_list.push_front(*p);
            }
            else
            {
                p->~persistent_buffer();
                std::lock_guard<spinlock> guard(pool_guard);
                pool.free(p);
            }
        }
    };
}

#endif