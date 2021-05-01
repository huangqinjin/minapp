#ifndef MINAPP_SERVLETS_HPP
#define MINAPP_SERVLETS_HPP

#include "attribute_set.hpp"
#include <string>
#include <stdexcept>

namespace minapp
{
    struct servlet_already_exists : public std::logic_error
    {
    public:
        explicit servlet_already_exists(const std::string& id)
            : std::logic_error("servlet already exists: " + id)
        {}
    };

    struct servlet_not_exists : public std::logic_error
    {
    public:
        explicit servlet_not_exists(const std::string& id)
            : std::logic_error("servlet not exists: " + id)
        {}
    };

    struct servlet_type_mismatch : public std::logic_error
    {
    public:
        explicit servlet_type_mismatch()
            : std::logic_error("servlet type mismatch")
        {}
    };

    struct servlet_params_error : public std::logic_error
    {
    public:
        explicit servlet_params_error()
            : std::logic_error("servlet params error")
        {}
    };

    struct servlets
    {
        attribute_set attrs;

        static std::string id(object::type_index type, attribute_set::key_t instance = {}) noexcept
        {
            attribute_set::key_t bin;
            object::type_index null = object::null_t();
            bin = attribute_set::key_t(reinterpret_cast<const char*>(&null), sizeof(null));
            std::size_t leading_zeros = 0, trailing_zeros = 0;
            while (leading_zeros < bin.size() && bin[leading_zeros] == 0) ++leading_zeros;
            while (trailing_zeros < bin.size() && bin[bin.size() - trailing_zeros - 1] == 0) ++trailing_zeros;
            bin = attribute_set::key_t(reinterpret_cast<const char*>(&type), sizeof(type));
            if (leading_zeros > trailing_zeros)
            {
                leading_zeros = 0;
                while (leading_zeros < bin.size() && bin[leading_zeros] == 0) ++leading_zeros;
                bin.remove_prefix(leading_zeros);
            }
            else
            {
                trailing_zeros = 0;
                while (trailing_zeros < bin.size() && bin[bin.size() - trailing_zeros - 1] == 0) ++trailing_zeros;
                bin.remove_suffix(trailing_zeros);
            }

            std::string name;
            name.reserve(2 + bin.size() * 2 + !instance.empty() + instance.size());
            name.push_back('S'); //prefix
            name.push_back(':'); //delimiter

            for (char c : bin) //type
            {
                const char* table = "0123456789ABCDEF";
                const auto uc = static_cast<unsigned char>(c);
                const char high = table[uc >> 4];
                const char low = table[uc & 0x0f];
                name.push_back(high);
                name.push_back(low);
            }

            if (!instance.empty())
            {
                name.push_back(':'); //delimiter
                name.append(instance); //suffix
            }
            return name;
        }

        template<typename T>
        static std::string id(attribute_set::key_t instance = {}) noexcept
        {
            return id(object::type_id<T>(), instance);
        }

        template<typename T>
        object::ptr<T> get(attribute_set::key_t instance = {}) try
        {
            std::string name = id<T>(instance);
            object srv = attrs.get(name);
            if (!srv) throw servlet_not_exists(name);
            return srv;
        }
        catch (bad_object_cast&)
        {
            throw servlet_type_mismatch();
        }

        template<typename T, typename ...Args>
        object::ptr<T> set(attribute_set::key_t instance = {}, Args&&... args)
        {
            object srv(std::in_place_type<T>, std::forward<Args>(args)...);
            attrs.set(id<T>(instance), srv);
            return srv;
        }

        template<typename T, typename ...Args>
        object::ptr<T> ret(attribute_set::key_t instance = {}, Args&&... args)
        {
            std::string name = id<T>(instance);
            object srv;
            attrs.compute(name, [&srv, &args...] (object& val) {
                if (!val) val = object(std::in_place_type<T>, std::forward<Args>(args)...);
                srv = val;
            });
            try
            {
                return srv;
            }
            catch (bad_object_cast&)
            {
                throw servlet_type_mismatch();
            }
        }

        template<typename T, typename ...Args>
        object::ptr<T> add(attribute_set::key_t instance = {}, Args&&... args)
        {
            std::string name = id<T>(instance);
            object srv;
            attrs.compute(name, [&srv, &args...] (object& val) {
                if (!val) srv = val = object(std::in_place_type<T>, std::forward<Args>(args)...);
            });
            if (!srv) throw servlet_already_exists(name);
            return srv;
        }

        template<typename T>
        object::ptr<T> del(attribute_set::key_t instance = {}) try
        {
            std::string name = id<T>(instance);
            object srv = attrs.remove(name);
            if (!srv) throw servlet_not_exists(name);
            return srv;
        }
        catch (bad_object_cast&)
        {
            throw servlet_type_mismatch();
        }

        template<typename T>
        object remove(attribute_set::key_t instance = {}) noexcept
        {
            return attrs.remove(id<T>(instance));
        }

        enum class ops { get, set, ret, add, del, };

        template<typename T, typename ...Args>
        object::ptr<T> op(ops op, attribute_set::key_t instance, Args&&... args)
        {
            std::string name = id<T>(instance);
            object srv;
            attrs.compute(name, [&name, &srv, op, &args...](object& val) {

                if (op == ops::get)
                {
                    if (val) srv = val;
                    else throw servlet_not_exists(name);
                }
                else if (op == ops::del)
                {
                    if (val) val.swap(srv);
                    else throw servlet_not_exists(name);
                }
                else if (val && op == ops::ret)
                {
                    srv = val;
                }
                else if (val && op == ops::add)
                {
                    throw servlet_already_exists(name);
                }
                else if constexpr (std::is_constructible_v<T, Args&&...>)
                {
                    srv.emplace<T>(std::forward<Args>(args)...);
                    val = srv;
                }
                else
                {
                    throw servlet_params_error();
                }
            });
            try
            {
                return srv;
            }
            catch (bad_object_cast&)
            {
                throw servlet_type_mismatch();
            }
        }
    };
}

#endif
