#ifndef MINAPP_ATTRIBUTE_SET_HPP
#define MINAPP_ATTRIBUTE_SET_HPP

#include <map>
#include <boost/any.hpp>
#include "spinlock.hpp"
#include <mutex>

namespace minapp
{
    // \todo copy is not thread-safe
    class attribute_set
    {
    public:
        using key_t = std::string;
        using value_t = boost::any;

        bool contains(const key_t& key) const
        {
            std::lock_guard<spinlock> _(guard);
            return attrs.count(key) > 0;
        }

        value_t set(key_t key, value_t value)
        {
            std::lock_guard<spinlock> _(guard);
            using std::swap;
            auto r = attrs.emplace(std::move(key), std::move(value));
            if(!r.second) swap(value, r.first->second);
            return value;
        }

        value_t get(const key_t& key) const
        {
            std::lock_guard<spinlock> _(guard);
            auto iter = attrs.find(key);
            if(iter == attrs.end()) return value_t{};
            return iter->second;
        }

        template<typename Value>
        Value get(const key_t& key) const
        {
            std::lock_guard<spinlock> _(guard);
            auto iter = attrs.find(key);
            if(iter == attrs.end())
                throw std::out_of_range("attribute_set::get()");
            return boost::any_cast<Value>(iter->second);
        }

        template<typename Value>
        bool get(const key_t& key, Value& value) const
        {
            std::lock_guard<spinlock> _(guard);
            auto iter = attrs.find(key);
            if(iter == attrs.end()) return false;
            value = boost::any_cast<const Value&>(iter->second);
            return true;
        }

        value_t remove(const key_t& key)
        {
            std::lock_guard<spinlock> _(guard);
            value_t value;
            auto iter = attrs.find(key);
            if(iter != attrs.end())
            {
                value = std::move(iter->second);
                attrs.erase(iter);
            }
            return value;
        }

    private:
        mutable spinlock guard;
        std::map<key_t, value_t> attrs;
    };
}

#endif //MINAPP_ATTRIBUTE_SET_HPP
