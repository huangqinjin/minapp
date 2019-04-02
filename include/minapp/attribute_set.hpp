#ifndef MINAPP_ATTRIBUTE_SET_HPP
#define MINAPP_ATTRIBUTE_SET_HPP

#include "object.hpp"
#include "spinlock.hpp"
#include <mutex>
#include <string_view>
#include <functional>

namespace minapp
{
    class attribute_set
    {
    public:
        using key_t = std::string_view;
        using value_t = object;

        attribute_set(const attribute_set&) = delete;
        attribute_set& operator=(const attribute_set&) = delete;
        attribute_set& operator=(attribute_set&&) = delete;

        attribute_set() noexcept;
        ~attribute_set() noexcept;
        attribute_set(attribute_set&& other) noexcept;
        void swap(attribute_set& other) noexcept;

        /**
         * @brief Test if contains the specified key.
         * @param key the key to test
         * @return true if and only if @p key is present whether the mapped value is null or not.
         */
        bool contains(key_t key) const noexcept;

        /**
         * @brief Query the value to which the specified key is mapped.
         * @param key the key to query
         * @return the value to which @p key is mapped (may be null),
         *         or null if no mapping for @p key.
         */
        value_t get(key_t key) const noexcept;

        /**
         * @brief Remove the entry for the specified key.
         * @param key the key whose mapping is to be removed
         * @return the value to which @p key mapped previously (may be null) or null if no mapping for @p key.
         */
        value_t remove(key_t key) noexcept;

        /**
         * @brief Remove the entry for the specified key only if currently mapped to the given value.
         * This is equivalent to:
         * @code
         * if (contains(key) && get(key) == value) {
         *     remove(key);
         *     return true;
         * } else {
         *     return false;
         * }
         * @endcode
         * @param key the key whose mapping is to be removed
         * @param value the value expected to be associated with the specified key
         * @return true if and only if the value was removed.
         */
        bool remove(key_t key, value_t value) noexcept;

        /**
         * @brief Map the specified key to the specified value.
         * @param key the key with which the specified value is to be associated
         * @param value the value to be associated with the specified key
         * @return the previous value associated with @p key, or null if no mapping for @p key.
         */
        value_t set(key_t key, value_t value);

        /**
         * @brief Map the specified key to the specified value if it is currently absent.
         * This is equivalent to:
         * @code
         * if (!contains(key)) {
         *     return set(key, value);
         * } else {
         *     return get(key);
         * }
         * @endcode
         * @param key the key with which the specified value is to be associated
         * @param value the value to be associated with the specified key
         * @return the value to which @p key mapped currently (may be null) or null if the entry was emplaced.
         */
        value_t emplace(key_t key, value_t value);

        /**
         * @brief Replace the value for the specified key only if it is currently present.
         * This is equivalent to:
         * @code
         * if (contains(key)) {
         *     return set(key, value);
         * } else {
         *     return {};
         * }
         * @endcode
         * @param key the key with which the specified value is to be associated
         * @param value the value to be associated with the specified key
         * @return the value to which @p key mapped previously (may be null) or null if no mapping for @p key.
         */
        value_t replace(key_t key, value_t value) noexcept;

        /**
         * @brief Replace the value for the specified key only if it is currently mapped to the given value.
         * This is equivalent to:
         * @code
         * if (contains(key) && get(key) == oldval) {
         *     set(key, newval);
         *     return true;
         * } else {
         *     return false;
         * }
         * @endcode
         * @param key the key with which the specified value is associated
         * @param oldval value expected to be associated with the specified key
         * @param newval value to be associated with the specified key
         * @return true if and only if the value was replaced
         */
        bool replace(key_t key, value_t oldval, value_t newval) noexcept;

        /**
         * @brief Compute the value for the specified key by giving current mapped value (or null if no
         * current mapping). If the computed value is null, the entry for the specified key is removed.
         * The entire method invocation is performed atomically. Operations on this set by other threads
         * will be blocked while transformation is in progress, so the computation should be short and
         * simple, and @b MUST @b NOT attempt to access this set.
         * This is equivalent to:
         * @code
         * auto value = get(key);
         * f(value);
         * if (value) {
         *     set(key, value);
         * } else {
         *     remove(key);
         * }
         * @endcode
         * @param key the key with which the specified value is to be associated
         * @param f the function to compute a value
         */
        void compute(key_t key, std::function<void(value_t&)> const& f);

        /**
         * @brief Iterate all entries.
         * @param f the action to be performed for each entry. If false is returned then the iteration stops.
         *          Although the key type is std::string_view, key.data() is a null-terminated string, so it
         *          is safe to pass key.data() to some functions that require C strings as arguments.
         *          Unlike compute(), the function call does not protected by lock, so it is safe to access
         *          or modify this set in the function body.
         * @return the number of entries iterated.
         */
        std::size_t foreach(std::function<bool(key_t, value_t)> const& f) const;

        /**
         * @brief Iterate all entries that keys are prefixed by the specified.
         * @param prefix witch the keys start with
         * @param f the action to be performed for each entry. If false is returned then the iteration stops.
         *          Although the key type is std::string_view, key.data() is a null-terminated string, so it
         *          is safe to pass key.data() to some functions that require C strings as arguments.
         *          Unlike compute(), the function call does not protected by lock, so it is safe to access
         *          or modify this set in the function body.
         * @return the number of entries iterated.
         */
        std::size_t foreach(key_t prefix, std::function<bool(key_t, value_t)> const& f) const;

        /**
         * @brief Iterate all entries that keys are between min and max.
         * @param min,max the interval the keys lies in
         * @param include_min,include_max indicates whether the interval is left/right-closed
         * @param f the action to be performed for each entry. If false is returned then the iteration stops.
         *          Although the key type is std::string_view, key.data() is a null-terminated string, so it
         *          is safe to pass key.data() to some functions that require C strings as arguments.
         *          Unlike compute(), the function call does not protected by lock, so it is safe to access
         *          or modify this set in the function body.
         * @return the number of entries iterated.
         */
        std::size_t foreach(key_t (min), key_t (max), bool include_min, bool include_max,
                            std::function<bool(key_t, value_t)> const& f) const;


        /// Value can't be reference type and always copy the value for safety.
        template<typename Value>
        std::enable_if_t<!std::is_reference_v<Value>, Value> get(key_t key) const
        {
            if(value_t value = get(key)) return object_cast<Value>(value);
            throw std::out_of_range("attribute_set::get()");
        }

        template<typename Value>
        bool get(key_t key, Value& value) const
        {
            value_t obj = get(key);
            if(auto p = object_cast<Value>(&obj))
            {
                value = *p;
                return true;
            }
            return false;
        }

    private:
        mutable spinlock guard;
        void* attrs[4];
    };
}

#endif //MINAPP_ATTRIBUTE_SET_HPP
