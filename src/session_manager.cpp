#include <minapp/session_manager.hpp>
#include <minapp/session.hpp>
#include <minapp/service.hpp>
#include <minapp/spinlock.hpp>

#include <mutex>

#include <boost/intrusive/set.hpp>

using namespace minapp;

namespace
{
    struct session_comparator
    {
        bool operator()(const session& s1, const session& s2) const
        {
            return s1.id() < s2.id();
        }

        bool operator()(std::uint64_t id, const session& s) const
        {
            return id < s.id();
        }

        bool operator()(const session& s, std::uint64_t id) const
        {
            return s.id() < id;
        }
    };

    struct session_impl : session, boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>>
    {
        session_impl(service_ptr service);
        ~session_impl();
    };

    struct manager_impl : session_manager
    {
        typedef boost::intrusive::set<session_impl,
                boost::intrusive::constant_time_size<false>,
                boost::intrusive::compare<session_comparator>> session_set;

        spinlock guard;
        session_set sessions;
    };

    session_impl::session_impl(service_ptr service) : session(std::move(service))
    {
        auto impl = static_cast<manager_impl*>(this->service()->manager().get());
        std::lock_guard<spinlock> guard(impl->guard);
        impl->sessions.insert(*this);
    }

    session_impl::~session_impl()
    {
        auto impl = static_cast<manager_impl*>(this->service()->manager().get());
        std::lock_guard<spinlock> guard(impl->guard);
        impl->sessions.erase(impl->sessions.iterator_to(*this));
    }
}

session_manager_ptr session_manager::create()
{
    return std::make_shared<manager_impl>();
}

session_ptr session_manager::create(service_ptr service)
{
    return std::make_shared<session_impl>(std::move(service));
}

session_ptr session_manager::get(unsigned long id)
{
    auto impl = static_cast<manager_impl*>(this);
    std::lock_guard<spinlock> guard(impl->guard);
    auto iter = impl->sessions.find(id, session_comparator{});
    if(iter != impl->sessions.end())
        return iter->shared_from_this();
    return session_ptr{};
}

std::size_t session_manager::foreach(object::fn<bool(&)(session*)> f)
{
    std::size_t c = 0;
    session_comparator comp{};
    auto impl = static_cast<manager_impl*>(this);
    impl->guard.lock();
    auto iter = impl->sessions.begin();
    while (iter != impl->sessions.end())
    {
        auto session = iter->shared_from_this();
        if (!session)
        {
            iter = impl->sessions.upper_bound(iter->id(), comp);
        }
        else
        {
            impl->guard.unlock();
            if(!f(session.get())) return c;
            ++c;
            auto id = session->id();
            session = nullptr;
            impl->guard.lock();
            iter = impl->sessions.upper_bound(id, comp);
        }
    }
    impl->guard.unlock();
    return c;
}

