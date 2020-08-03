#ifndef OBJECT_HPP
#define OBJECT_HPP

#include <type_traits>
#include <atomic>
#include <utility>        // move, exchange, in_place_type_t
#include <memory>         // uninitialized_value_construct_n, destroy_n
#include <new>            // operator new, operator delete, align_val_t, bad_array_new_length

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
        std::atomic<long> refcount = ATOMIC_VAR_INIT(1);

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
            class fn : public holder, public holder::template fn<F>
            {
                using base = holder::fn<F>;

                R call(Args... args) override
                {
                    return static_cast<R>(base::value()(std::forward<Args>(args)...));
                }

                [[noreturn]] void throws() override { throw std::addressof(base::value()); }

            public:
                explicit fn(T&& t, A&&... a) : base(is_in_place_type<D>{}, std::forward<T>(t), std::forward<A>(a)...) {}
            };

            return new fn(std::forward<T>(t), std::forward<A>(a)...);
        }
    };

public:
    template<typename F>
    class fn;

    template<typename T>
    class ptr;

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

    template<typename Object, typename = std::enable_if_t<std::is_same_v<Object, object>>>
    fn(const Object& obj)
    {
        if (obj.type() != object::type_id<R(Args...)>()) throw object_not_fn{};
        object::operator=(obj);
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
    fn(const fn<R(Args...)>& f) noexcept : o((void*)std::addressof(f)), f(&callobj) {}

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
        if (obj.type() != object::type_id<T>()) throw bad_object_cast{};
        object::operator=(obj);
    }

    ptr(object&& obj)
    {
        if (obj.type() != object::type_id<T>()) throw bad_object_cast{};
            swap(obj);
    }

    T* operator->() const noexcept
    {
        return unsafe_object_cast<T>(const_cast<object*>(this));
    }

    [[nodiscard]] T& operator*() const noexcept
    {
        return *operator->();
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
