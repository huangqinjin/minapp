#ifndef MINAPP_PERSISTENT_BUFFER_HPP
#define MINAPP_PERSISTENT_BUFFER_HPP

#include <boost/any.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/asio/buffer.hpp>

namespace minapp
{
    class persistent_buffer :
        public boost::intrusive::slist_base_hook<>,
        public boost::asio::const_buffer
    {

    public:
        typedef boost::any underlying_type;
        underlying_type underlying_;

        typedef boost::asio::const_buffer base_type;

        base_type& base()
        {
            return *this;
        }

        const base_type& base() const
        {
            return *this;
        }

        persistent_buffer()
            : base_type(boost::asio::mutable_buffer{})
        {
        }

        persistent_buffer(const const_buffer& b)
            : base_type(b)
        {
        }

        persistent_buffer(const persistent_buffer& other)
            : base_type(other)
        {
        }

        persistent_buffer(persistent_buffer&& other)
            : base_type(other), underlying_(static_cast<underlying_type&&>(other.underlying_))
        {
        }

        persistent_buffer& operator=(const persistent_buffer& other)
        {
            base() = other;
            underlying_.clear();
            return *this;
        }

        persistent_buffer& operator=(persistent_buffer&& other)
        {
            base() = other;
            underlying_ = static_cast<underlying_type&&>(other.underlying_);
            return *this;
        }

        boost::asio::const_buffer reader() const
        {
            return base();
        }

        boost::asio::mutable_buffer writer()
        {
            return { const_cast<void*>(data()), size() };
        }

        bool has_underlying() const
        {
            return !underlying_.empty();
        }

        underlying_type& underlying()
        {
            return underlying_;
        }

        template<typename C>
        persistent_buffer& persist(C&& c) &
        {
            underlying_ = std::forward<C>(c);
            return *this;
        }

        template<typename C>
        persistent_buffer&& persist(C&& c) &&
        {
            return static_cast<persistent_buffer&&>(persist(std::forward<C>(c)));
        }
    };

    inline persistent_buffer persist(const boost::asio::const_buffer& b)
    {
        return persistent_buffer{ b };
    }

    template<typename C>
    persistent_buffer persist(const boost::asio::const_buffer& b, C&& c)
    {
        persistent_buffer buffer(b);
        buffer.underlying() = std::forward<C>(c);
        return static_cast<persistent_buffer&&>(buffer);
    }

    template<typename C>
    persistent_buffer persist(C&& c,
        typename std::enable_if<!std::is_convertible<C&&, const boost::asio::const_buffer&>::value>::type* = 0)
    {
        persistent_buffer::underlying_type holder(std::forward<C>(c));
        persistent_buffer buffer(boost::asio::buffer(boost::any_cast<C&>(holder)));
        buffer.underlying() = std::move(holder);
        return static_cast<persistent_buffer&&>(buffer);
    }

    class persistent_buffer_list :
            public boost::intrusive::slist<persistent_buffer,
                    boost::intrusive::constant_time_size<false>,
                    boost::intrusive::cache_last<true>,
                    boost::intrusive::linear<false>> {};

    template<typename ...Buffers>
    inline persistent_buffer_list make_list(Buffers&... buffers)
    {
        persistent_buffer_list list;
        for (persistent_buffer& b : { std::ref(buffers)... })
            list.push_back(b);
        return static_cast<persistent_buffer_list&&>(list);
    }
}
#endif