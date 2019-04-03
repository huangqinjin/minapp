#ifndef OBJECT_HPP
#define OBJECT_HPP

#include <type_traits>
#include <atomic>
#include <typeinfo>
#include <utility>        // move, exchange
#include <memory>         // uninitialized_value_construct_n, destroy_n
#include <new>            // operator new, operator delete, align_val_t, bad_array_new_length

class bad_object_cast : public std::exception {};

class object
{
    template<typename T>
    using rmcvr = std::remove_cv_t<std::remove_reference_t<T>>;

    template<typename T, typename U = rmcvr<T>>
    using enable = std::enable_if_t<!std::is_same_v<object, U>, U>;

    class placeholder
    {
        std::atomic<long> refcount = ATOMIC_VAR_INIT(1);

    public:
        long addref(long c = 1) noexcept { return c + refcount.fetch_add(c, std::memory_order_relaxed); }
        long release(long c = 1) noexcept { return addref(-c); }
        virtual ~placeholder() = default;
        virtual const std::type_info& type() const noexcept = 0;
        [[noreturn]] virtual void throws() { throw nullptr; }
    } *p;

    template<typename T>
    class holder : public placeholder
    {
        template<std::size_t I, std::size_t N, typename U>
        U& get(U(&a)[N]) { return a[I]; }

        template<std::size_t I, std::size_t N, typename U>
        U&& get(U(&&a)[N]) { return static_cast<U&&>(a[I]); }

        template<typename ...Args>
        struct is_one : public std::false_type { using type = void; };

        template<typename Arg>
        struct is_one<Arg> : public std::true_type { using type = Arg; };

        T v;
        const std::type_info& type() const noexcept final { return typeid(T); }

        [[noreturn]] void throws() final { throw std::addressof(v); }

        template<typename... Args>
        explicit holder(std::true_type, Args&&... args) : v(std::forward<Args>(args)...) {}

        template<typename... Args>
        explicit holder(std::false_type, Args&&... args) : v{std::forward<Args>(args)...} {}

        template<typename A, std::size_t... I>
        explicit holder(std::index_sequence<I...>, A&& a) : v{get<I>(static_cast<A&&>(a))...} {}

    public:
        T& value() noexcept
        {
            return v;
        }

        template<typename... Args>
        static auto create(Args&&... args)
        {
            using O = is_one<Args...>;
            using U = rmcvr<typename O::type>;
            if constexpr (std::is_array_v<T> && O::value && std::is_same_v<T, U>)
            {
                return new holder(std::make_index_sequence<std::extent_v<U>>{}, std::forward<Args>(args)...);
            }
            else
            {
                return new holder(std::is_constructible<T, Args&&...>{}, std::forward<Args>(args)...);
            }
        }
    };

    template<typename T>
    class holder<T[]> : public placeholder
    {
        const std::ptrdiff_t n;
        T v[1];

        const std::type_info& type() const noexcept final { return typeid(T[]); }

        explicit holder(std::ptrdiff_t n) : n(n), v{}
        {
            std::uninitialized_value_construct_n(v + 1, n - 1);
        }

        ~holder() override
        {
            std::destroy_n(v + 1, n - 1);
        }

        void* operator new(std::size_t sz, std::ptrdiff_t n)
        {
            return ::operator new(sz + (n - 1) * sizeof(T), std::align_val_t{alignof(holder)});
        }

        // called in create() if the constructor throws an exception, could be private
        void operator delete(void* ptr, std::ptrdiff_t)
        {
            operator delete(ptr);
        }

    public:
        // called in destructor of object when delete, must be public
        void operator delete(void* ptr)
        {
            ::operator delete(ptr, std::align_val_t{alignof(holder)});
        }

        auto value() noexcept -> T(&)[]
        {
            return reinterpret_cast<T(&)[]>(v);
        }

        static auto create(std::ptrdiff_t n)
        {
            if(n < 1) throw std::bad_array_new_length();
            return new(n) holder(n);
        }
    };

    explicit object(placeholder* p) noexcept : p(p) {}

public:
    object() noexcept : p(nullptr) {}

    object(object&& obj) noexcept : p(obj.p)
    {
        obj.p = nullptr;
    }

    object(const object& obj) noexcept : p(obj.p)
    {
        if(p) p->addref();
    }

    ~object() noexcept
    {
        if(p && p->release() == 0)
            delete p;
    }

    object exchange(const object& obj) noexcept
    {
        object old(std::exchange(p, obj.p));
        if(p) p->addref();
        return old;
    }

    object exchange(object&& obj) noexcept
    {
        object old(std::exchange(p, obj.p));
        obj.p = nullptr;
        return old;
    }

    object& operator=(const object& obj) noexcept
    {
        if(p != obj.p) exchange(obj);
        return *this;
    }

    object& operator=(object&& obj) noexcept
    {
        if(p != obj.p) exchange(std::move(obj));
        return *this;
    }

    void swap(object& obj) noexcept
    {
        std::swap(p, obj.p);
    }

    explicit operator bool() const noexcept
    {
        return p != nullptr;
    }

    const std::type_info& type() const noexcept
    {
        return p ? p->type() : typeid(void);
    }

    bool operator==(const object& obj) const noexcept { return p == obj.p; }
    bool operator!=(const object& obj) const noexcept { return p != obj.p; }
    bool operator< (const object& obj) const noexcept { return p <  obj.p; }
    bool operator> (const object& obj) const noexcept { return p >  obj.p; }
    bool operator<=(const object& obj) const noexcept { return p <= obj.p; }
    bool operator>=(const object& obj) const noexcept { return p >= obj.p; }

public:
    template<typename ValueType, typename U = enable<ValueType>, typename... Args>
    decltype(auto) emplace(Args&&... args)
    {
        auto q = holder<U>::create(std::forward<Args>(args)...);
        object old(std::exchange(p, q));
        return q->value();
    }

    template<typename ValueType, typename U = enable<ValueType>>
    object(ValueType&& value) : p(holder<U>::create(std::forward<ValueType>(value))) {}

    template<typename ValueType, typename U = enable<ValueType>>
    object& operator=(ValueType&& value)
    {
        object(std::forward<ValueType>(value)).swap(*this);
        return *this;
    }

public:
    template<typename ValueType>
    friend const ValueType* unsafe_object_cast(const object* obj) noexcept;

    template<typename ValueType>
    friend const ValueType* object_cast(const object* obj) noexcept;

    template<typename ValueType>
    friend const ValueType* polymorphic_object_cast(const object* obj) noexcept;
};


template<typename ValueType>
const ValueType* unsafe_object_cast(const object* obj) noexcept
{
    return std::addressof(static_cast<object::holder<object::rmcvr<ValueType>>*>(obj->p)->value());
}

template<typename ValueType>
const ValueType* object_cast(const object* obj) noexcept
{
    if(obj && obj->p && obj->p->type() == typeid(object::rmcvr<ValueType>))
        return unsafe_object_cast<ValueType>(obj);
    return nullptr;
}

template<typename ValueType>
const ValueType* polymorphic_object_cast(const object* obj) noexcept
{
    try { if (obj && obj->p) obj->p->throws(); }
    catch (const ValueType* p) { return p; }
    catch (...) {}
    return nullptr;
}


#define CAST(cast) \
template<typename ValueType> ValueType* cast(object* obj) noexcept \
{ return (ValueType*)(cast<ValueType>(const_cast<const object*>(obj))); } \
template<typename ValueType> ValueType& cast(object& obj) \
{ if(auto p = cast<ValueType>(std::addressof(obj))) return *p; throw bad_object_cast{}; } \
template<typename ValueType> const ValueType& cast(const object& obj) \
{ if(auto p = cast<ValueType>(std::addressof(obj))) return *p; throw bad_object_cast{}; } \


CAST(unsafe_object_cast)
CAST(object_cast)
CAST(polymorphic_object_cast)
#undef CAST


#endif //OBJECT_HPP
