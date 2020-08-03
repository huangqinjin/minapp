#ifndef MINAPP_PERSISTENT_BUFFER_HPP
#define MINAPP_PERSISTENT_BUFFER_HPP

#include "object.hpp"
#include <boost/intrusive/slist.hpp>
#include <boost/asio/buffer.hpp>

namespace minapp
{
    /**
     * @brief const_buffer with persistent storage shared.
     *
     *  @b Note: Always set storage first and then set buffer referring data in storage.
     *  A common bad use is:
     *  @code
     *  std::string msg = "hello, world";
     *  persistent_buffer buf;
     *  buf.buffer() = boost::asio::buffer(msg);
     *  buf.storage() = std::move(msg);
     *  @endcode
     *
     *  std::string uses short string optimization (SSO). The msg is located in stack
     *  and so buffer refers to stack which is fault. The solution is replace std::string
     *  with std::vector or
     *  @code
     *  auto& storage = buf.storage().emplace<std::string>(std::move(msg));
     *  buf.buffer() = boost::asio::buffer(storage);
     *  @endcode
     *
     *  @sa persist()
     */
    class persistent_buffer :
        public boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
        public boost::asio::const_buffer
    {
        object storage_;

        template<typename C>
        static decltype(boost::asio::buffer(std::declval<C&>())) test(int&&);

        template<typename C>
        static void test(const int&);

    public:
        template<typename C>
        struct is_const_buffer : std::is_same<decltype(test<C>(0)), boost::asio::const_buffer> {};
        template<typename C>
        struct is_mutable_buffer : std::is_same<decltype(test<C>(0)), boost::asio::mutable_buffer> {};

        object& storage() { return storage_; }
        const_buffer& buffer() { return *this; }
        object storage() const { return storage_; }
        const_buffer buffer() const { return *this; }
    };


    /**
     *  @p object: using this object as the underlying storage and typename A specifies the actual type.
     * 
     *  @p persistent_buffer: copy persistent_buffer and @p n is the maximum of buffer size in bytes.
     *
     *  @p pointer: buffer is exactly an @b array_with_length_n and data are copied into storage.
     *
     *  @p mutable_buffer: buffer is mutable_buffer with no more than @p n bytes and data are copied into storage.
     *
     *  @p const_buffer: buffer is const_buffer with no more than @p n bytes and data are @b NOT copied. It's the
     *                   user's responsibility to keep data available during the lifetime of returned persistent_buffer
     *                   and its copies.
     *
     *  @p Container: buffer is boost::asio::buffer(Container, n) if it's a legal function call and Container is copied
     *                or moved into storage. @b NOTE: @b 1. For some non-owning containers, though they are copied into
     *                storage, user should also keep the underlying data, so prefer const_buffer (no copy) in the case.
     *                @b 2. For some read-only containers and views like std::array<const T, N> and std::string_view,
     *                it acts as const_buffer and no copy performed.
     *
     *  @p POD: buffer is the POD itself with no more than @p n bytes. POD is copied into storage.
     */
    template<typename A = object, typename C>
    persistent_buffer persist(C&& c, std::size_t n = -1)
    {
        using NR = std::remove_reference_t<C>;
        using NC = std::remove_cv_t<NR>;

        static_assert(!std::is_reference_v<A> && !std::is_same_v<NC, std::remove_cv_t<A>>,
                      "Actual type is required as the first template parameter to persist an object.");

        persistent_buffer buf;

        if constexpr (std::is_same_v<NC, object>)
        {
            buf.buffer() = boost::asio::buffer(object_cast<A>(c), n);
            buf.storage() = std::forward<C>(c);
        }
        else if constexpr (std::is_convertible_v<C&&, persistent_buffer>)
        {
            buf = static_cast<persistent_buffer>(std::forward<C>(c));
            buf.buffer() = boost::asio::buffer(buf.buffer(), n);
        }
        else if constexpr (std::is_pointer_v<NR>)
        {
            using NP = std::remove_pointer_t<NR>;
            using T = std::conditional_t<std::is_void_v<NP>, std::byte, std::remove_cv_t<NP>>;
            auto p = buf.storage().emplace<T[]>(n);
            std::memcpy(p, c, n * sizeof(T));
            buf.buffer() = boost::asio::buffer(p, n * sizeof(T));
        }
        else if constexpr (std::is_convertible_v<C&&, boost::asio::mutable_buffer>)
        {
            auto b = boost::asio::buffer(static_cast<boost::asio::mutable_buffer>(std::forward<C>(c)), n);
            buf = persist(b.data(), b.size());
        }
        else if constexpr (persistent_buffer::is_const_buffer<NC>::value) // std::array<const T, N>, std::string_view
        {
            buf.buffer() = boost::asio::buffer(std::forward<C>(c), n);
        }
        else if constexpr (persistent_buffer::is_const_buffer<const NC>::value)  // "string literal", T[N], std::vector<T>
        {
            auto& storage = buf.storage().emplace<NR>(std::forward<C>(c));
            buf.buffer() = boost::asio::buffer(storage, n);
        }
        else
        {
            auto& storage = buf.storage().emplace<NR>(std::forward<C>(c));    // POD
            buf.buffer() = boost::asio::buffer(std::addressof(storage), (std::min)(sizeof(storage), n));
        }

        return buf;
    }

    /** The list become invalidate if any element's link is modified, e.g. destructed or added to another list */
    class persistent_buffer_list :
            public boost::intrusive::slist<persistent_buffer,
                    boost::intrusive::constant_time_size<false>,
                    boost::intrusive::cache_last<true>,
                    boost::intrusive::linear<true>> {};

    static_assert(boost::asio::is_const_buffer_sequence<persistent_buffer_list>::value);

    template<typename ...Buffers>
    std::enable_if_t<std::conjunction_v<std::is_same<persistent_buffer, Buffers>...>, persistent_buffer_list>
    make_list(Buffers&... buffers)
    {
        persistent_buffer_list list;
        ((void) list.push_back(buffers), ...);
        return list;
    }
}
#endif
