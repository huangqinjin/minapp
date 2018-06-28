#ifndef MINAPP_ATTRIBUTE_SET_HPP
#define MINAPP_ATTRIBUTE_SET_HPP

#include <map>
#include <boost/any.hpp>

namespace minapp
{
    class attribute_set
    {
    public:
        using key_t = std::string;
        using value_t = boost::any;

        std::map<key_t, value_t> attrs;

        bool contains(const key_t& key) const
        {
            return attrs.count(key) > 0;
        }

        value_t set(key_t key, value_t value)
        {
            using std::swap;
            auto r = attrs.emplace(std::move(key), std::move(value));
            if(!r.second) swap(value, r.first->second);
            return value;
        }

        value_t* get(const key_t& key)
        {
            auto iter = attrs.find(key);
            if(iter == attrs.end()) return nullptr;
            return &iter->second;
        }

        const value_t* get(const key_t& key) const
        {
            auto iter = attrs.find(key);
            if(iter == attrs.end()) return nullptr;
            return &iter->second;
        }

        value_t remove(const key_t& key)
        {
            value_t value;
            auto iter = attrs.find(key);
            if(iter != attrs.end())
            {
                value = std::move(iter->second);
                attrs.erase(iter);
            }
            return value;
        }
    };
}

#endif //MINAPP_ATTRIBUTE_SET_HPP
