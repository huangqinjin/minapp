#include <minapp/attribute_set.hpp>

#include <cstring>    // memcpy
#include <boost/intrusive/set.hpp>

using namespace minapp;

namespace
{
    struct nvp : boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>
    {
        object value;
        std::size_t len;

        char* str() const { return (char*)(&len + 1); }

        void* operator new(std::size_t sz, bool, std::size_t len)
        {
            return ::operator new(sz + len + 1);
        }

        void operator delete(void* ptr, bool, std::size_t)
        {
            operator delete(ptr);
        }

        void operator delete(void* ptr)
        {
            ::operator delete(ptr);
        }

        static nvp& create(attribute_set::key_t key)
        {
            auto p = new (true, key.size()) nvp;
            p->len = key.size();
            std::memcpy(p->str(), key.data(), key.size());
            p->str()[p->len] = '\0';
            return *p;
        }
    };

    struct name
    {
        using type = attribute_set::key_t;
        type operator()(const nvp& nvp) const noexcept { return type(nvp.str(), nvp.len); }
    };

    typedef boost::intrusive::set<nvp,
            boost::intrusive::constant_time_size<false>,
            boost::intrusive::key_of_value<name>> nvps;

    nvps& map(void* (&attrs)[4])
    {
        static_assert(sizeof(nvps) <= sizeof(attrs), "boost::intrusive::set structure error");
        return reinterpret_cast<nvps&>(attrs);
    }

    const nvps& map(void* const (&attrs)[4])
    {
        static_assert(sizeof(nvps) <= sizeof(attrs), "boost::intrusive::set structure error");
        return reinterpret_cast<const nvps&>(attrs);
    }
}


attribute_set::attribute_set() noexcept
{
    new (attrs) nvps;
}

attribute_set::~attribute_set() noexcept
{
    map(attrs).clear_and_dispose(std::default_delete<nvp>{});
    map(attrs).~nvps();
}

attribute_set::attribute_set(attribute_set&& other) noexcept
{
    std::lock_guard<spinlock> _(other.guard);
    new (attrs) nvps(std::move(map(other.attrs)));
}

void attribute_set::swap(attribute_set& other) noexcept
{
    std::lock(guard, other.guard);
    map(attrs).swap(map(other.attrs));
    guard.unlock();
    other.guard.unlock();
}

bool attribute_set::contains(key_t key) const noexcept
{
    std::lock_guard<spinlock> _(guard);
    return map(attrs).count(key) > 0;
}

attribute_set::value_t attribute_set::get(key_t key) const noexcept
{
    std::lock_guard<spinlock> _(guard);
    auto iter = map(attrs).find(key);
    if(iter == map(attrs).end()) return value_t{};
    return iter->value;
}

attribute_set::value_t attribute_set::remove(key_t key) noexcept
{
    std::unique_ptr<nvp> p;
    std::lock_guard<spinlock> _(guard);
    auto iter = map(attrs).find(key);
    if(iter != map(attrs).end())
    {
        p.reset(&*iter);
        map(attrs).erase(iter);
    }
    return p ? std::move(p->value) : value_t{};
}

bool attribute_set::remove(key_t key, value_t value) noexcept
{
    std::unique_ptr<nvp> p;
    std::lock_guard<spinlock> _(guard);
    auto iter = map(attrs).find(key);
    if(iter != map(attrs).end() && iter->value == value)
    {
        p.reset(&*iter);
        map(attrs).erase(iter);
    }
    return !!p;
}


attribute_set::value_t attribute_set::set(key_t key, value_t value)
{
    nvps::insert_commit_data ctx;
    std::lock_guard<spinlock> _(guard);
    auto r = map(attrs).insert_check(key, ctx);
    if (r.second)
    {
        auto& p = nvp::create(key);
        p.value.swap(value);
        map(attrs).insert_commit(p, ctx);
    }
    else
    {
        r.first->value.swap(value);
    }
    return value;
}

attribute_set::value_t attribute_set::emplace(key_t key, value_t value)
{
    nvps::insert_commit_data ctx;
    std::lock_guard<spinlock> _(guard);
    auto r = map(attrs).insert_check(key, ctx);
    if (r.second)
    {
        auto& p = nvp::create(key);
        p.value.swap(value);
        map(attrs).insert_commit(p, ctx);
        return value;
    }
    return r.first->value;
}

attribute_set::value_t attribute_set::replace(key_t key, value_t value) noexcept
{
    std::lock_guard<spinlock> _(guard);
    auto iter = map(attrs).find(key);
    if(iter != map(attrs).end())
    {
        iter->value.swap(value);
        return value;
    }
    return {};
}

bool attribute_set::replace(key_t key, value_t oldval, value_t newval) noexcept
{
    std::lock_guard<spinlock> _(guard);
    auto iter = map(attrs).find(key);
    if(iter != map(attrs).end() && iter->value == oldval)
    {
        iter->value.swap(newval);
        return true;
    }
    return false;
}

void attribute_set::compute(key_t key, object::fn<void(&)(value_t&)> f)
{
    std::unique_ptr<nvp> p;
    value_t value;
    nvps::insert_commit_data ctx;
    std::lock_guard<spinlock> _(guard);
    auto r = map(attrs).insert_check(key, ctx);
    if (r.second)
    {
        f(value);
        if (value)
        {
            auto& p = nvp::create(key);
            p.value.swap(value);
            map(attrs).insert_commit(p, ctx);
        }
    }
    else
    {
        value = r.first->value;
        f(value);
        if (value)
        {
            r.first->value.swap(value);
        }
        else
        {
            p.reset(&*r.first);
            map(attrs).erase(r.first);
        }
    }
}

std::size_t attribute_set::foreach(object::fn<bool(&)(key_t, value_t)> f) const
{
    std::size_t c = 0;
    std::string key(15, '\0');
    std::unique_lock<spinlock> guard(this->guard);
    auto iter = map(attrs).begin();
    while (iter != map(attrs).end())
    {
        auto& p = *iter;
        key_t key1 = name{}(p);
        key.assign(key1);
        value_t value = p.value;

        guard.unlock();
        if(!f(key, std::move(value))) return c;
        ++c;

        guard.lock();
        iter = map(attrs).upper_bound(key);
    }
    return c;
}

std::size_t attribute_set::foreach(key_t prefix, object::fn<bool(&)(key_t, value_t)> f) const
{
    std::size_t c = 0;
    std::string key(15, '\0');
    std::unique_lock<spinlock> guard(this->guard);
    auto iter = map(attrs).lower_bound(prefix);
    while (iter != map(attrs).end())
    {
        auto& p = *iter;
        key_t key1 = name{}(p);

        // C++20
        // if (!key.starts_with(prefix))
        if (key1.substr(0, prefix.size()) != prefix)
            break;

        key.assign(key1);
        value_t value = p.value;

        guard.unlock();
        if(!f(key, std::move(value))) return c;
        ++c;

        guard.lock();
        iter = map(attrs).upper_bound(key);
    }
    return c;
}

std::size_t attribute_set::foreach(key_t (min), key_t (max), bool include_min, bool include_max,
                                   object::fn<bool(&)(key_t, value_t)> f) const
{
    std::size_t c = 0;
    std::string key(15, '\0');
    std::unique_lock<spinlock> guard(this->guard);
    auto iter = include_min ? map(attrs).lower_bound((min)) : map(attrs).upper_bound((min));
    while (iter != map(attrs).end())
    {
        auto& p = *iter;
        key_t key1 = name{}(p);

        if (key1.compare((max)) >= include_max)
            break;

        key.assign(key1);
        value_t value = p.value;

        guard.unlock();
        if(!f(key, std::move(value))) return c;
        ++c;

        guard.lock();
        iter = map(attrs).upper_bound(key);
    }
    return c;
}
