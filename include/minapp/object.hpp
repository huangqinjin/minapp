#ifndef OBJECT_HPP
#define OBJECT_HPP

#include <type_traits>
#include <atomic>
#include <utility>        // move, exchange, in_place_type_t
#include <memory>         // uninitialized_value_construct_n, destroy_n
#include <new>            // operator new, operator delete, align_val_t, bad_array_new_length
#include <stdexcept>      // out_of_range
#include <algorithm>      // copy

class bad_object_cast : public std::exception {};
class object_not_fn : public bad_object_cast {};

class object
{
    template<typename T>
    static void t() noexcept {}

public:
    using type_index = void (*)() noexcept;

    template<typename T>
    [[nodiscard]] static type_index type_id() noexcept
    {
        return &t<std::remove_cv_t<T>>;
    }

    [[nodiscard]] static type_index null_t() noexcept
    {
        return type_id<void>();
    }

protected:
    template<typename ...Args>
    struct is_args_one : std::false_type { using type = void; };

    template<typename Arg>
    struct is_args_one<Arg> : std::true_type { using type = Arg; };

    template<typename T>
    struct is_in_place_type : std::false_type { using type = T; };

    template<typename T>
    struct is_in_place_type<std::in_place_type_t<T>> : std::true_type { using type = T; };

    template<typename T>
    using rmcvr = std::remove_cv_t<std::remove_reference_t<T>>;

    template<typename T, typename U = rmcvr<T>>
    using enable = std::enable_if_t<!std::is_base_of_v<object, U> && !is_in_place_type<U>::value, U>;

    template<typename T>
    class held
    {
        T t;

        template<std::size_t I, std::size_t N, typename U>
        static U& get(U(&a)[N]) { return a[I]; }

        template<std::size_t I, std::size_t N, typename U>
        static U&& get(U(&&a)[N]) { return static_cast<U&&>(a[I]); }

        template<typename... Args>
        held(std::true_type, Args&&... args) : t(std::forward<Args>(args)...) {}

        template<typename... Args>
        held(std::false_type, Args&&... args) : t{std::forward<Args>(args)...} {}

        template<typename A, std::size_t... I>
        held(std::index_sequence<I...>, A&& a) : t{get<I>(static_cast<A&&>(a))...} {}

    public:
        using type = T;

        T& value() noexcept { return t; }

        template<typename... Args, typename U = rmcvr<typename is_args_one<Args...>::type>,
                 typename = std::enable_if_t<std::is_array_v<T> && std::is_same_v<T, U>>>
        held(int&&, Args&&... args)
            : held(std::make_index_sequence<std::extent_v<U>>{}, std::forward<Args>(args)...) {}

        template<typename... Args>
        held(const int&, Args&&... args)
            : held(std::is_constructible<T, Args&&...>{}, std::forward<Args>(args)...) {}
    };

    class placeholder
    {
        std::atomic<long> refcount = 1;

    public:
        long addref(long c = 1) noexcept { return c + refcount.fetch_add(c, std::memory_order_relaxed); }
        long release(long c = 1) noexcept { return addref(-c); }
        virtual ~placeholder() = default;
        [[nodiscard]] virtual type_index type() const noexcept = 0;
        [[noreturn]] virtual void throws() { throw nullptr; }
    } *p;

    template<typename T>
    class holder : public placeholder, public held<T>
    {
        [[nodiscard]] type_index type() const noexcept final { return type_id<T>(); }

        [[noreturn]] void throws() final { throw std::addressof(this->value()); }

        template<typename... Args>
        explicit holder(Args&&... args) : held<T>(0, std::forward<Args>(args)...) {}

    public:
        template<typename... Args>
        [[nodiscard]] static auto create(Args&&... args)
        {
            return new holder(std::forward<Args>(args)...);
        }
    };

    template<typename T>
    class holder<T[]> : public placeholder
    {
        const std::ptrdiff_t n;
        T v[1];

        [[nodiscard]] type_index type() const noexcept final { return type_id<T[]>(); }

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

        std::ptrdiff_t length() const noexcept
        {
            return n;
        }

        [[nodiscard]] static auto create(std::ptrdiff_t n)
        {
            if(n < 1) throw std::bad_array_new_length();
            return new(n) holder(n);
        }
    };

    template<typename R, typename... Args>
    class holder<R(Args...)> : public placeholder
    {
        [[nodiscard]] type_index type() const noexcept override { return type_id<R(Args...)>(); }

        template<typename F>
        class fn : public held<F>
        {
        public:
            std::remove_pointer_t<F>& value() noexcept
            {
                if constexpr (std::is_pointer_v<F>) return *held<F>::value();
                else return held<F>::value();
            }

            template<typename... A>
            explicit fn(std::false_type, A&&... a) : held<F>(0, std::forward<A>(a)...) {}

            template<typename T, typename... A>
            explicit fn(std::true_type, T&&, A&&... a) : fn(std::false_type{}, std::forward<A>(a)...) {}
        };

    public:
        virtual R call(Args... args) = 0;

        template<typename T, typename... A, typename D = std::decay_t<T>,
                 typename F = typename is_in_place_type<D>::type,
                 typename = std::enable_if_t<std::is_invocable_r_v<R, F, Args...>>>
        [[nodiscard]] static auto create(T&& t, A&&... a)
        {
            class impl : public holder, public fn<F>
            {
                using base = fn<F>;

                R call(Args... args) override
                {
                    return static_cast<R>(base::value()(std::forward<Args>(args)...));
                }

                [[noreturn]] void throws() override { throw std::addressof(base::value()); }

            public:
                explicit impl(T&& t, A&&... a) : base(is_in_place_type<D>{}, std::forward<T>(t), std::forward<A>(a)...) {}
            };

            return new impl(std::forward<T>(t), std::forward<A>(a)...);
        }
    };

public:
    class atomic;

    template<typename F>
    class fn;

    template<typename T>
    class ptr;

    template<typename T>
    class ref;

    template<typename T>
    class vec;

    using handle = placeholder*;

    explicit object(handle p) noexcept : p(p) {}

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

    object& operator=(const object& obj) noexcept
    {
        if(p != obj.p) object(obj).swap(*this);
        return *this;
    }

    object& operator=(object&& obj) noexcept
    {
        if(p != obj.p) object(std::move(obj)).swap(*this);
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

    [[nodiscard]] type_index type() const noexcept
    {
        return p ? p->type() : null_t();
    }

    [[nodiscard]] handle release() noexcept
    {
        return std::exchange(p, nullptr);
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

    template<typename ValueType, typename... Args>
    object(std::in_place_type_t<ValueType>, Args&&... args)
        : p(holder<rmcvr<ValueType>>::create(std::forward<Args>(args)...)) {}

public:
    template<typename ValueType>
    friend ValueType* unsafe_object_cast(object* obj) noexcept;

    template<typename ValueType>
    friend ValueType* object_cast(object* obj) noexcept;

    template<typename ValueType>
    friend ValueType* polymorphic_object_cast(object* obj) noexcept;
};

template<typename ValueType>
ValueType* unsafe_object_cast(object* obj) noexcept
{
    return std::addressof(static_cast<object::holder<object::rmcvr<ValueType>>*>(obj->p)->value());
}

template<typename ValueType>
ValueType* object_cast(object* obj) noexcept
{
    if(obj && obj->p && obj->p->type() == object::type_id<object::rmcvr<ValueType>>())
        return unsafe_object_cast<ValueType>(obj);
    return nullptr;
}

template<typename ValueType>
ValueType* polymorphic_object_cast(object* obj) noexcept
{
    try { if (obj && obj->p) obj->p->throws(); }
    catch (ValueType* p) { return p; }
    catch (...) {}
    return nullptr;
}


#define CAST(cast) \
template<typename ValueType> std::add_const_t<ValueType>* cast(const object* obj) noexcept \
{ return cast<ValueType>(const_cast<object*>(obj)); } \
template<typename ValueType> ValueType& cast(object& obj) \
{ if(auto p = cast<ValueType>(std::addressof(obj))) return *p; throw bad_object_cast{}; } \
template<typename ValueType> std::add_const_t<ValueType>& cast(const object& obj) \
{ if(auto p = cast<ValueType>(std::addressof(obj))) return *p; throw bad_object_cast{}; } \


CAST(unsafe_object_cast)
CAST(object_cast)
CAST(polymorphic_object_cast)
#undef CAST


class object::atomic
{
    using storage_t = std::uintptr_t;
    enum : storage_t
    {
        mask = 3,
        locked = 1,
        waiting = 2,
    };

    mutable std::atomic<storage_t> storage;
    static_assert(sizeof(handle) <= sizeof(storage_t), "atomic::storage cannot hold object::handle");
    static_assert(alignof(placeholder) >= (1 << 2), "2 low order bits are needed by object::atomic");

    handle lock_and_load(std::memory_order order) const noexcept
    {
        auto v = storage.load(order);
        while (true) switch (v & mask)
        {
            case 0: // try to lock
                if (storage.compare_exchange_weak(v, v | locked,
                    std::memory_order_acquire, std::memory_order_relaxed))
                    return reinterpret_cast<handle>(v);
                break;
            case locked: // try to wait
                if (!storage.compare_exchange_weak(v, (v & ~mask) | waiting,
                    std::memory_order_relaxed, std::memory_order_relaxed))
                    break; // try again
                v = (v & ~mask) | waiting;
                [[fallthrough]];
            case waiting: // just wait
#if defined(__cpp_lib_atomic_wait)
                storage.wait(v, std::memory_order_relaxed);
#endif
                v = storage.load(std::memory_order_relaxed);
                break;
            default:
                std::terminate();
        }
    }

    void store_and_unlock(handle h, std::memory_order order) const noexcept
    {
        auto v = storage.exchange(reinterpret_cast<storage_t>(h), order);
#if defined(__cpp_lib_atomic_wait)
        // notify all waiters since waiting mask has been cleared.
        if ((v & mask) == waiting) storage.notify_all();
#else
        (void)v;
#endif
    }

public:
    static constexpr bool is_always_lock_free = false;
    bool is_lock_free() const noexcept { return is_always_lock_free; }

    atomic(const atomic&) = delete;
    atomic& operator=(const atomic&) = delete;
    atomic() noexcept : storage(storage_t{}) {}

    atomic(object obj) noexcept
        : storage(reinterpret_cast<storage_t>(obj.release())) {}

    ~atomic() noexcept
    {
        auto v = storage.load(std::memory_order_relaxed);
        (void)object(reinterpret_cast<handle>(v));
    }

    // need non-const version, otherwise constructor of object would be used
    // during implicit conversion from non-const atomic to object.
    operator object() noexcept
    {
        return load();
    }

    operator object() const noexcept
    {
        return load();
    }

    object operator=(object desired) noexcept
    {
        store(desired);
        return std::move(desired);
    }

    void store(object desired, std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        exchange(std::move(desired), order);
    }

    object load(std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        if (order != std::memory_order_seq_cst) order = std::memory_order_relaxed;
        object obj(lock_and_load(order));
        if (obj.p) obj.p->addref();
        store_and_unlock(obj.p, std::memory_order_release);
        return obj;
    }

    object exchange(object desired, std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        if (order != std::memory_order_seq_cst) order = std::memory_order_release;
        object obj(lock_and_load(std::memory_order_relaxed));
        store_and_unlock(desired.release(), order);
        return obj;
    }

    bool compare_exchange_weak(object& expected, object desired,
                               std::memory_order success,
                               std::memory_order failure) noexcept
    {
        return compare_exchange_strong(expected, std::move(desired), success, failure);
    }

    bool compare_exchange_weak(object& expected, object desired,
                               std::memory_order order =
                               std::memory_order_seq_cst) noexcept
    {
        return compare_exchange_strong(expected, std::move(desired), order);
    }

    bool compare_exchange_strong(object& expected, object desired,
                                 std::memory_order success,
                                 std::memory_order failure) noexcept
    {
        if (success != std::memory_order_seq_cst) success = std::memory_order_release;
        if (failure != std::memory_order_seq_cst) failure = std::memory_order_release;
        object obj(lock_and_load(std::memory_order_relaxed));
        if (obj == expected)
        {
            store_and_unlock(desired.release(), success);
            return true;
        }
        else
        {
            if (obj.p) obj.p->addref();
            store_and_unlock(obj.p, failure);
            obj.swap(expected);
            return false;
        }
    }

    bool compare_exchange_strong(object& expected, object desired,
                                 std::memory_order order =
                                 std::memory_order_seq_cst) noexcept
    {
        return compare_exchange_strong(expected, std::move(desired), order, order);
    }

#if defined(__cpp_lib_atomic_wait)
    void wait(object old, std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        return storage.wait(old.p, order);
    }

    void notify_one() noexcept
    {
        return storage.notify_one();
    }

    void notify_all() noexcept
    {
        return storage.notify_all();
    }
#endif

    //////////////////
    //// spinlock ////
    //////////////////
    bool try_lock() noexcept
    {
        auto v = storage.load(std::memory_order_relaxed) & ~mask;
        return storage.compare_exchange_weak(v, v | locked,
               std::memory_order_acquire, std::memory_order_relaxed);
    }

    void lock() noexcept
    {
        (void)lock_and_load(std::memory_order_relaxed);
    }

    void unlock() noexcept
    {
        auto v = storage.load(std::memory_order_relaxed) & ~mask;
        store_and_unlock(reinterpret_cast<handle>(v), std::memory_order_release);
    }

    object get() const noexcept
    {
        auto v = storage.load(std::memory_order_relaxed);
        object obj(reinterpret_cast<handle>(v & ~mask));
        if (obj.p) obj.p->addref();
        return obj;
    }

    object set(object obj) noexcept
    {
        auto o = reinterpret_cast<storage_t>(obj.p);
        auto v = storage.load(std::memory_order_relaxed);
        while (!storage.compare_exchange_weak(v, o | (v & mask), std::memory_order_relaxed));
        obj.p = reinterpret_cast<handle>(v & ~mask);
        return std::move(obj);
    }
};

template<typename R, typename... Args>
class object::fn<R(Args...)> : public object
{
    template<typename F>
    using enable = std::enable_if_t<!std::is_base_of_v<fn, F> &&
                                    std::is_invocable_r_v<R, F, Args...>>;
public:
    fn() noexcept = default;

    template<typename T, typename F = typename is_in_place_type<std::decay_t<T>>::type,
             typename = enable<F>, typename... A>
    fn(T&& t, A&&... a) : object(std::in_place_type<R(Args...)>, std::forward<T>(t), std::forward<A>(a)...) {}

    template<typename Object, typename = std::enable_if_t<std::is_same_v<rmcvr<Object>, object>>>
    fn(Object&& obj)
    {
        if (obj && obj.type() != type_id<R(Args...)>()) throw object_not_fn{};
        object::operator=(std::forward<Object>(obj));
    }

    void swap(fn& f) noexcept
    {
        return object::swap(f);
    }

    template<typename T, typename F = std::decay_t<T>,
            typename = std::enable_if_t<!is_in_place_type<F>::value>,
            typename = enable<F>, typename... A>
    decltype(auto) emplace(A&&... a)
    {
        return object::emplace<R(Args...)>(std::in_place_type<F>, std::forward<A>(a)...);
    }

    R operator()(Args... args) const
    {
        if (p == nullptr) throw object_not_fn{};
        return static_cast<holder<R(Args...)>*>(p)->call(std::forward<Args>(args)...);
    }
};

template<typename R, typename... Args>
class object::fn<R(&)(Args...)>         // std::function_ref
{
    void* o;
    R (*f)(void*, Args...);

    static R callobj(void* o, Args... args)
    {
        return static_cast<R>((*static_cast<fn<R(Args...)>*>(o))(
                std::forward<Args>(args)...));
    }

public:
    fn(const fn<R(Args...)>& f) : o((void*)std::addressof(f)), f(&callobj)
    {
        if (!f) throw object_not_fn{};
    }

    template<typename F, typename = std::enable_if_t<!std::is_base_of_v<fn<R(Args...)>, rmcvr<F>> &&
                                                     !std::is_base_of_v<fn<R(&)(Args...)>, rmcvr<F>> &&
                                                     std::is_invocable_r_v<R, F, Args...>>>
    fn(F&& f) noexcept : o((void*)std::addressof(f))
    {
        this->f = [](void* o, Args... args) -> R
        {
            return static_cast<R>((*(std::add_pointer_t<F>)(o))(
                    std::forward<Args>(args)...));
        };
    }

    template<typename Object, typename = std::enable_if_t<std::is_same_v<Object, object>>>
    fn(const Object& obj) : o((void*)std::addressof(obj)), f(&callobj)
    {
        if (obj.type() != object::type_id<R(Args...)>()) throw object_not_fn{};
    }

    fn<R(Args...)> object() const noexcept
    {
        return f == &callobj ? *static_cast<fn<R(Args...)>*>(o) : fn<R(Args...)>{};
    }

    R operator()(Args... args) const
    {
        return (*f)(o, std::forward<Args>(args)...);
    }
};

template<typename T>
class object::ptr : public object
{
public:
    ptr(const object& obj)
    {
        if (obj && obj.type() != type_id<T>()) throw bad_object_cast{};
        object::operator=(obj);
    }

    ptr(object&& obj)
    {
        if (obj && obj.type() != type_id<T>()) throw bad_object_cast{};
        object::swap(obj);
    }

    void swap(ptr& p) noexcept
    {
        return object::swap(p);
    }

    template<typename... Args>
    decltype(auto) emplace(Args&&... args)
    {
        return object::emplace<T>(std::forward<Args>(args)...);
    }

    T* operator->()
    {
        if (p == nullptr) throw bad_object_cast{};
        return unsafe_object_cast<T>(this);
    }

    const T* operator->() const
    {
        if (p == nullptr) throw bad_object_cast{};
        return unsafe_object_cast<T>(this);
    }

    [[nodiscard]] T& operator*()
    {
        return *operator->();
    }

    [[nodiscard]] const T& operator*() const
    {
        return *operator->();
    }
};

template<typename T>
class object::ref : public object
{
public:
    ref(const object& obj)
    {
        if (obj.type() != type_id<T>()) throw bad_object_cast{};
        object::operator=(obj);
    }

    ref(object&& obj)
    {
        if (obj.type() != type_id<T>()) throw bad_object_cast{};
        object::swap(obj);
    }

    ref(const ptr<T>& p)
    {
        if (!p) throw bad_object_cast{};
        object::operator=(p);
    }

    ref(ptr<T>&& p)
    {
        if (!p) throw bad_object_cast{};
        object::swap(p);
    }

    void swap(ref& r) noexcept
    {
        return object::swap(r);
    }

    [[nodiscard]] handle release() const noexcept
    {
        return object(*this).release();
    }

    template<typename... Args>
    decltype(auto) emplace(Args&&... args)
    {
        return object::emplace<T>(std::forward<Args>(args)...);
    }

    [[nodiscard]] ptr<T> operator&() const noexcept
    {
        ptr<T> p;
        object(*this).swap(p);
        return p;
    }

    operator T&() noexcept
    {
        return *unsafe_object_cast<T>(this);
    }

    operator const T&() const noexcept
    {
        return *unsafe_object_cast<T>(this);
    }

    T* operator->() noexcept
    {
        return unsafe_object_cast<T>(this);
    }

    const T* operator->() const noexcept
    {
        return unsafe_object_cast<T>(this);
    }

#ifdef OBJECT_HAVE_OPERATOR_DOT // someday we could have this
    T& operator.() noexcept
    {
        return *this;
    }

    const T& operator.() const noexcept
    {
        return *this;
    }
#endif
};

template<typename T>
class object::vec<T&>         // std::span
{
public:
    using element_type     = T;
    using value_type       = std::remove_cv_t<T>;
    using size_type        = std::size_t;
    using difference_type  = std::ptrdiff_t;
    using pointer          = element_type*;
    using const_pointer    = const element_type*;
    using reference        = element_type&;
    using const_reference  = const element_type&;
    using iterator         = pointer;
//  using reverse_iterator = std::reverse_iterator<iterator>;

    template<typename R>
    vec(R&& r) noexcept : p(std::data(r)), n(std::size(r)) {}

    vec() noexcept : p(nullptr), n(0) {}
    vec(pointer p, size_type n) noexcept : p(p), n(n) {}
    pointer data() const noexcept { return p; }
    size_type size() const noexcept { return n; }
    bool empty() const noexcept { return n == 0; }
    iterator begin() const noexcept { return p; }
    iterator end() const noexcept { return p + n; }
    reference front() const noexcept { return p[0]; }
    reference back() const noexcept { return p[n - 1]; }
    reference operator[](size_type i) const noexcept { return p[i]; }
    size_type size_bytes() const noexcept { return n * sizeof(element_type); }
    vec subspan(size_type offset, size_type count) const noexcept { return {p + offset, count}; }
    vec first(size_type count) const noexcept { return {p, count}; }
    vec last(size_type count) const noexcept { return {p + (n - count), count}; }

private:
    pointer p;
    size_type n;
};

template<typename T>
class object::vec : public object
{
public:
    vec() = default;

    explicit vec(std::size_t n)
    {
        if (n != 0) object::emplace<T[]>(n);
    }

    vec(const object& obj)
    {
        if (obj && obj.type() != type_id<T[]>()) throw bad_object_cast{};
        object::operator=(obj);
    }

    vec(object&& obj)
    {
        if (obj && obj.type() != type_id<T[]>()) throw bad_object_cast{};
        object::swap(obj);
    }

    vec(std::initializer_list<T> list)
    {
        if (list.size() == 0) return;
        std::copy(list.begin(), list.end(), object::emplace<T[]>(list.size()));
    }

    void swap(vec& v) noexcept
    {
        return object::swap(v);
    }

    vec<T&> emplace(std::ptrdiff_t n)
    {
        if (n != 0) object::emplace<T[]>(n);
        else object().swap(*this);
        return *this;
    }

    operator vec<T&>() noexcept
    {
        if (auto h = static_cast<holder<T[]>*>(p))
            return { h->value(), static_cast<std::size_t>(h->length()) };
        return {};
    }

    operator vec<const T&>() const noexcept
    {
        return const_cast<vec*>(this)->operator vec<T&>();
    }

    T* data() noexcept
    {
        if (auto h = static_cast<holder<T[]>*>(p))
            return h->value();
        return nullptr;
    }

    const T* data() const noexcept
    {
        return const_cast<vec*>(this)->data();
    }

    std::size_t size() const noexcept
    {
        if (auto h = static_cast<holder<T[]>*>(p))
            return static_cast<std::size_t>(h->length());
        return 0;
    }

    bool empty() const noexcept
    {
        return p == nullptr;
    }

    T& operator[](std::size_t i) noexcept
    {
        return static_cast<holder<T[]>*>(p)->value()[i];
    }

    const T& operator[](std::size_t i) const noexcept
    {
        return const_cast<vec*>(this)->operator[](i);
    }

    T& at(std::size_t i)
    {
        if (size() <= i) throw std::out_of_range("object::vec::at()");
        return static_cast<holder<T[]>*>(p)->value()[i];
    }

    const T& at(std::size_t i) const
    {
        return const_cast<vec*>(this)->at(i);
    }

    T* begin() noexcept
    {
        if (auto h = static_cast<holder<T[]>*>(p))
            return h->value();
        return nullptr;
    }

    T* end() noexcept
    {
        if (auto h = static_cast<holder<T[]>*>(p))
            return h->value() + h->length();
        return nullptr;
    }

    const T* begin() const noexcept
    {
        return const_cast<vec*>(this)->begin();
    }

    const T* end() const noexcept
    {
        return const_cast<vec*>(this)->end();
    }
};


#ifdef cobject_handle_copy
#undef cobject_handle_copy
#endif
#ifdef cobject_handle_clear
#undef cobject_handle_clear
#endif
#define cobject_handle_copy(p) (void*)object((const object&)(p)).release()
#define cobject_handle_clear(p) (void)object((object::handle)(p))


#endif //OBJECT_HPP
