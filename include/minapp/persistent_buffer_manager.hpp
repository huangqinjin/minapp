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
        persistent_buffer_list cached_buffers_list;
        persistent_buffer_list managed_buffers_list_1;
        persistent_buffer_list managed_buffers_list_2;
        spinlock pool_guard;
        spinlock cached_buffers_list_guard;
        spinlock managed_buffers_list_1_guard;
        long marker_;
        std::pair<persistent_buffer*, persistent_buffer*> cached_buffers;

    public:
        persistent_buffer_manager(unsigned cached = 8)
            : pool(sizeof(persistent_buffer)), marker_(0)
        {
            persistent_buffer* p = (persistent_buffer*) pool.ordered_malloc(cached);
            cached_buffers.first = p;
            cached_buffers.second = p + cached;
            while (p < cached_buffers.second)
                cached_buffers_list.push_front(*::new((void*)p++) persistent_buffer);
        }

        void manage(persistent_buffer& b)
        {
            persistent_buffer& buf = hold_buffer(b);
            std::lock_guard<spinlock> guard(managed_buffers_list_1_guard);
            managed_buffers_list_1.push_back(buf);
        }

        void manage(persistent_buffer&& b)
        {
            return manage(b);
        }

        template<typename ...Buffers>
        void manage(Buffers&&... buffers)
        {
            persistent_buffer_list tmp;
            for (persistent_buffer& b : { std::ref(buffers)... })
                tmp.push_back(hold_buffer(b));

            std::lock_guard<spinlock> guard(managed_buffers_list_1_guard);
            managed_buffers_list_1.splice(managed_buffers_list_1.end(), tmp);  // Constant time
        }

        void manage(persistent_buffer_list& list)
        {
            persistent_buffer_list tmp;
            for (persistent_buffer& b : list)
                tmp.push_back(hold_buffer(b));

            std::lock_guard<spinlock> guard(managed_buffers_list_1_guard);
            managed_buffers_list_1.splice(managed_buffers_list_1.end(), tmp);  // Constant time
        }

        void manage(persistent_buffer_list&& list)
        {
            return manage(list);
        }

        std::int64_t marker()
        {
            return marker_;
        }

        std::int64_t mark()
        {
            if (managed_buffers_list_2.empty())
            {
                std::lock_guard<spinlock> guard(managed_buffers_list_1_guard);
                if (!managed_buffers_list_1.empty())
                {
                    managed_buffers_list_2 = std::move(managed_buffers_list_1);
                    return ++marker_;
                }
            }
            return -marker_;
        }

        persistent_buffer_list& marked()
        {
            return managed_buffers_list_2;
        }

        void clear_marked()
        {
            managed_buffers_list_2.clear_and_dispose([this](persistent_buffer_list::pointer p)
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
            managed_buffers_list_1.clear_and_dispose(deleter);
            managed_buffers_list_2.clear_and_dispose(deleter);
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

            p->underlying() = std::move(b.underlying());
            return *p;
        }

        void free_buffer(persistent_buffer* p)
        {
            if (p >= cached_buffers.first && p < cached_buffers.second)
            {
                p->underlying().clear();
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