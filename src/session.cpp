#include <boost/version.hpp>
#if BOOST_VERSION >= 107000
#  ifndef BOOST_ASIO_NO_DYNAMIC_BUFFER_V1
#    define BOOST_ASIO_NO_DYNAMIC_BUFFER_V1
#  endif
#else
#  undef BOOST_ASIO_NO_DYNAMIC_BUFFER_V1
#endif

#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/service.hpp>
#include <minapp/session_manager.hpp>
#include <minapp/spinlock.hpp>

#include <mutex>

#if BOOST_VERSION >= 107400
#include <boost/stl_interfaces/iterator_interface.hpp>
#else
#include <boost/iterator/iterator_facade.hpp>
#endif
#include <boost/range/iterator_range_core.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <boost/intrusive/set.hpp>

using namespace minapp;

// GCC, clang, MSVC support [ , ## __VA_ARGS__ ] for empty __VA_ARGS__
#undef CALLBACK
#define CALLBACK(...) [self = shared_from_this(), ## __VA_ARGS__](const boost::system::error_code& ec, std::size_t bytes_transferred)

namespace
{
#ifndef BOOST_ASIO_NO_DYNAMIC_BUFFER_V1

    struct dynamic_buffer_adaptor
    {
        triple_buffer& impl;
        const std::size_t maximum;

        using const_buffers_type = triple_buffer::const_buffers_type;
        using mutable_buffers_type = triple_buffer::mutable_buffers_type;

        dynamic_buffer_adaptor(const dynamic_buffer_adaptor&) = delete;
        dynamic_buffer_adaptor(dynamic_buffer_adaptor&&) = default;

        const_buffers_type data() const
        {
            return impl.internal_input_buffer();
        }

        std::size_t size() const
        {
            return impl.internal_input_buffer().size();
        }

        std::size_t max_size() const
        {
            return (std::min)(impl.max_size() - impl.external_input_buffer().size(), maximum);
        }

        std::size_t capacity() const
        {
            // Note that capacity may be greater than max_size
            return impl.capacity() - impl.external_input_buffer().size();
        }

        mutable_buffers_type prepare(std::size_t n)
        {
            // Note that size may be greater than max_size
            if (size() > max_size() || max_size() - size() < n)
                throw std::length_error("dynamic_buffer too long");
            return impl.prepare_output_buffer(n);
        }

        void commit(std::size_t n)
        {
            impl.commit_to_internal_input(n);
        }

        void consume(std::size_t n); // [async_]read_util() do not need consume, and it's requirement
                                     // of DynamicBuffer is not compatible with triple_buffer.
                                     // Declare it to meet the DynamicBuffer requirements and no define
                                     // it to catch error if it is called anywhere.
    };

#else

    struct dynamic_buffer_adaptor
    {
        triple_buffer& impl;
        const std::size_t maximum;

        using const_buffers_type = triple_buffer::const_buffers_type;
        using mutable_buffers_type = triple_buffer::mutable_buffers_type;

        mutable_buffers_type data(std::size_t pos, std::size_t n)
        {
            return boost::asio::buffer(boost::asio::buffer(
                    impl.internal_input_buffer().data(), size()) + pos, n);
        }

        const_buffers_type data(std::size_t pos, std::size_t n) const
        {
            return boost::asio::buffer(boost::asio::buffer(
                    impl.internal_input_buffer().data(), size()) + pos, n);
        }

        std::size_t size() const
        {
            return (std::min)(impl.size() - impl.external_input_buffer().size(), max_size());
        }

        std::size_t max_size() const
        {
            return (std::min)(impl.max_size() - impl.external_input_buffer().size(), maximum);
        }

        std::size_t capacity() const
        {
            return (std::min)(impl.capacity() - impl.external_input_buffer().size(), max_size());
        }

        void grow(std::size_t n)
        {
            if (max_size() - size() < n)
                throw std::length_error("dynamic_buffer too long");
            impl.grow_output_buffer(n);
        }

        void shrink(std::size_t n)
        {
            impl.shrink_output_buffer(n);
        }

        void consume(std::size_t n); // [async_]read_util() do not need consume, and it's requirement
                                     // of DynamicBuffer is not compatible with triple_buffer.
                                     // Declare it to meet the DynamicBuffer requirements and no define
                                     // it to catch error if it is called anywhere.
    };

#endif

    static_assert(boost::asio::is_dynamic_buffer<dynamic_buffer_adaptor>::value,
                  "dynamic_buffer_adaptor should model the boost::asio::DynamicBuffer concept");


    const std::size_t size_msb = std::size_t(-1) - (std::size_t(-1) >> 1u);

    const std::string crlf = { '\r', '\n' };

    std::atomic_ulong id = 1;


    struct session_impl : session, boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>>
    {
        explicit session_impl(service_ptr service);
        ~session_impl();
    };

    struct session_id
    {
        using type = unsigned long;
        type operator()(const session& s) const noexcept { return s.id(); }
    };

    struct manager_impl : session_manager
    {
        typedef boost::intrusive::set<session_impl,
                boost::intrusive::constant_time_size<false>,
                boost::intrusive::key_of_value<session_id>> session_set;

        spinlock guard;
        session_set sessions;
    };
}

session_impl::session_impl(service_ptr service) : session(std::move(service))
{
    auto impl = static_cast<manager_impl*>(this->service()->manager().get());
    std::lock_guard<spinlock> guard(impl->guard);
    impl->sessions.insert(*this);
}

session_impl::~session_impl()
{
    auto impl = static_cast<manager_impl*>(this->service()->manager().get());
    std::lock_guard<spinlock> guard(impl->guard);
    impl->sessions.erase(impl->sessions.iterator_to(*this));
}

session::session(service_ptr service)
    : id_(::id.fetch_add(1, std::memory_order_relaxed)),
      service_(std::move(service)), socket_(*service_->context()),
      protocol_(protocol::any), protocol_options_{},
      status_(status::connecting), read_buffer_size_(65536)
{

}

session::~session()
{
    close(true);
}

unsigned long session::id() const
{
    return id_;
}

std::size_t session::read_buffer_size() const
{
    return read_buffer_size_;
}

void session::read_buffer_size(std::size_t sz)
{
    read_buffer_size_ = sz;
}

const std::string& session::delimiter() const
{
    return delimiter_;
}

void session::delimiter(char delim)
{
    delimiter_.clear();
    delimiter_.push_back(delim);
}

void session::delimiter(std::string delim)
{
    delimiter_ = std::move(delim);
}

enum protocol session::protocol() const
{
    return protocol_;
}

enum protocol_options session::protocol_options() const
{
    return protocol_options_;
}

void session::protocol(enum protocol protocol, enum protocol_options options)
{
    protocol_ = protocol;
    protocol_options_ = options;
}

void session::protocol(enum protocol protocol, std::size_t bufsz, enum protocol_options options)
{
    this->protocol(protocol, options);
    this->read_buffer_size(bufsz);
}

enum status session::status() const
{
    return status_;
}

minapp::socket& session::socket()
{
    return socket_;
}

const service_ptr& session::service() const
{
    return service_;
}

handler_ptr session::handler() const
{
    return handler_;
}

handler_ptr session::handler(handler_ptr other)
{
    other = noexcept_handler::wrap(std::move(other));
    std::swap(other, handler_);
    return other;
}

handler_ptr session::use_service_handler()
{
    handler_ptr other = service_->handler();
    std::swap(other, handler_);
    return other;
}

void session::close(bool immediately)
{
    auto current = status_.load(std::memory_order_relaxed);
    const auto target = immediately ? status::closed : status::closing;

    while (current < target && !status_.compare_exchange_weak(
           current, target, std::memory_order_seq_cst, std::memory_order_relaxed));

    if (current >= target) return;

    if (immediately)
    {
        boost::system::error_code ignored;
        socket_.shutdown(socket::shutdown_both, ignored);
        socket_.close(ignored);
        handler()->close(this);
    }
    else
    {
        boost::system::error_code ignored;
        socket_.shutdown(socket::shutdown_receive, ignored);
        write();
    }
}

context& session::execution_context() const
{
    return *service_->context();
}

bool session::check(const boost::system::error_code& ec)
{
    //if (this->status_ >= status::closing) return false;
    if (ec)
    {
        handler()->error(this, ec);
        close(false);
        return false;
    }
    return true;
}

std::future<session_ptr> session::connect(const endpoint& ep)
{
    std::promise<session_ptr> promise;
    std::future<session_ptr> future = promise.get_future();
    socket_.async_connect(ep,
    [self = shared_from_this(), promise = std::move(promise)]
    (const boost::system::error_code& ec) mutable
    {
        self->connect(ec, &promise);
    });

    return future;
}

std::future<session_ptr> session::connect(object::fn<endpoint()> gen)
{
    // boost::generator_iterator and boost::function_input_iterator do not support sentinel iterators.
    struct generator_iterator :
#if BOOST_VERSION >= 107400
        boost::stl_interfaces::iterator_interface<generator_iterator, std::forward_iterator_tag, const endpoint>
    {
        using iterator_interface::operator++;
        const endpoint& operator*() const { return value; }
        generator_iterator& operator++() { value = gen(); return *this; }
        bool operator==(const generator_iterator& other) const { return value == other.value; }
#else
        boost::iterators::iterator_facade<generator_iterator, const endpoint, boost::iterators::single_pass_traversal_tag>
    {
        const endpoint& dereference() const { return value; }
        void increment() { value = gen(); }
        bool equal(const generator_iterator& other) const { return value == other.value; }
#endif
        endpoint value;
        object::fn<endpoint()> gen;

        generator_iterator() : gen(), value() {}
        explicit generator_iterator(object::fn<endpoint()> gen) : value(gen()), gen(std::move(gen)) { }
    };

    std::promise<session_ptr> promise;
    std::future<session_ptr> future = promise.get_future();
    boost::asio::async_connect(socket_, generator_iterator(std::move(gen)), generator_iterator(),
    [self = shared_from_this(), promise = std::move(promise)]
    (const boost::system::error_code& ec, const generator_iterator&) mutable
    {
        self->connect(ec, &promise);
    });

    return future;
}

bool session::connect(const boost::system::error_code& ec, std::promise<session_ptr>* promise)
{
    if (ec)
    {
        handler()->error(this, ec);
        close(true);
        if (promise) promise->set_exception(std::make_exception_ptr(boost::system::system_error(ec)));
        return false;
    }
    else
    {
        status_ = status::connected;
        handler()->connect(this, socket_.remote_endpoint());
        read();
        if (promise) promise->set_value(shared_from_this());
        return true;
    }
}

void session::write(persistent_buffer_list& list)
{
    if (status_ >= status::closing) return;
    write_queue_.manage(list);
    write();
}

void session::write()
{
    if (status_ >= status::closed) return;
    auto marker = write_queue_.mark();
    if (marker <= 0)
    {
        if (marker == 0 && status_ == status::closing)
            close(true);
        return;
    }

    boost::asio::async_write(socket_, boost::make_iterator_range(write_queue_.marked()), CALLBACK()
    {
        if (ec)
        {
            self->handler()->error(self.get(), ec);
            self->close(true);
        }
        else
        {
            self->handler()->write(self.get(), self->write_queue_.marked());
            self->write_queue_.clear_marked();
            self->write();
        }
    });
}

void session::read()
{
    auto current = status::connected;
    if (!status_.compare_exchange_strong(current, status::reading) && current != status::reading)
        return;

    if(!has_options(protocol_options_, protocol_options::do_not_consume_buffer))
        buf_.consume_whole_external_input();
    buf_.mark_current_external_input();

    switch (protocol_)
    {
    case protocol::none:
        current = status::reading;
        status_.compare_exchange_strong(current, status::connected);
        break;

    case protocol::any:
        read_some(read_buffer_size_);
        break;

    case protocol::fixed:
        read_fixed(read_buffer_size_);
        break;

    case protocol::delim:
        read_delim(delimiter_);
        break;

    case protocol::delim_zero:
        read_delim('\0');
        break;

    case protocol::delim_cr:
        read_delim('\r');
        break;

    case protocol::delim_lf:
        read_delim('\n');
        break;

    case protocol::delim_crlf:
        read_delim(crlf);
        break;

    case protocol::prefix_8:
        read_prefix(1);
        break;

    case protocol::prefix_16:
        read_prefix(2);
        break;

    case protocol::prefix_32:
        read_prefix(4);
        break;

    case protocol::prefix_64:
        read_prefix(8);
        break;

    case protocol::prefix_var:
        read_prefix(size_msb);
        break;

    default:
        this->check(make_error_code(boost::system::errc::protocol_not_supported));
    }
}

void session::read_some(std::size_t bufsize)
{
    assert(bufsize <= read_buffer_size_);
    if(buf_.internal_input_buffer().size() > 0)
    {
        buf_.commit_to_external_input(bufsize);
        buf_.move_to_new_external_input_segment();
        handler()->read(this, buf_);
        read();
    }
    else
    {
        socket_.async_read_some(buf_.prepare_output_buffer(bufsize), CALLBACK()
        {
            if (!self->check(ec)) return;
            self->buf_.commit_to_internal_input(bytes_transferred);
            self->buf_.commit_whole_internal_input();
            self->buf_.move_to_new_external_input_segment();
            self->handler()->read(self.get(), self->buf_);
            self->read();
        });
    }
}

void session::read_fixed(std::size_t bufsize)
{
    assert(bufsize <= read_buffer_size_);
    const std::size_t remaining = buf_.internal_input_buffer().size();
    if (remaining >= bufsize)
    {
        buf_.commit_to_external_input(bufsize);
        buf_.move_to_new_external_input_segment();
        handler()->read(this, buf_);
        read();
    }
    else
    {
        boost::asio::async_read(socket_, buf_.prepare_output_buffer(bufsize - remaining), CALLBACK()
        {
            if (!self->check(ec)) return;
            self->buf_.commit_to_internal_input(bytes_transferred);
            self->buf_.commit_whole_internal_input();
            self->buf_.move_to_new_external_input_segment();
            self->handler()->read(self.get(), self->buf_);
            self->read();
        });
    }
}

void session::read_delim(char delim)
{
    std::string s;
    s.push_back(delim);
    read_delim(s);
}

void session::read_delim(const std::string& delim)
{
    std::size_t delim_length = delim.size();
    if (delim_length == 0) return read_some(read_buffer_size_);

    const bool ignore = has_options(protocol_options_, protocol_options::ignore_protocol_bytes);
    auto callback = CALLBACK(delim_length, ignore)
    {
        if (!self->check(ec)) return;

#ifdef BOOST_ASIO_NO_DYNAMIC_BUFFER_V1
        // Unlike DynamicBuffer_v1, the whole output buffer of DynamicBuffer_v2 contains valid data
        // since it had been shrunk before the callback. So commit the whole output buffer.
        self->buf_.commit_to_internal_input(self->buf_.output_buffer().size());
#endif
        // buf has already been committed and bytes_transferred is the length from the beginning of
        // internal input to the delimiter (including), so it may be smaller than the size of internal input.
        self->buf_.commit_to_external_input(bytes_transferred - delim_length * ignore);
        self->buf_.move_to_new_external_input_segment();
        self->handler()->read(self.get(), self->buf_);
        self->buf_.commit_to_external_input(delim_length * ignore);
        self->read();
    };

#ifdef BOOST_ASIO_NO_DYNAMIC_BUFFER_V1
    // DynamicBuffer_v2 treats the output buffer as valid data, so remove it before read.
    buf_.shrink_output_buffer(buf_.output_buffer().size());
#endif

    if (delim_length == 1)
    {
        boost::asio::async_read_until(socket_, dynamic_buffer_adaptor{buf_, read_buffer_size_},
                                      delim[0], std::move(callback));
    }
    else
    {
        boost::asio::async_read_until(socket_, dynamic_buffer_adaptor{buf_, read_buffer_size_},
                                      delim, std::move(callback));
    }
}

void session::read_prefix(std::size_t len)
{
    const std::size_t remaining = buf_.internal_input_buffer().size();
    auto p = (const unsigned char*)buf_.internal_input_buffer().data();
    const bool var = len >= size_msb;
    if(var) len -= size_msb;

    if (len == 0)
    {
        while (len < remaining && (p[len] & 0x80u) != 0) ++len;
        ++len;
    }

    if (remaining < len || (var && (p[len - 1] & 0x80u) != 0 && (++len, true)))
    {
        if (len > read_buffer_size_)
        {
            this->check(make_error_code(boost::system::errc::message_size));
        }
        else
        {
            std::size_t sz = len - remaining;
            if (var) len += size_msb;
            boost::asio::async_read(socket_, buf_.prepare_output_buffer(sz), CALLBACK(len)
            {
                if (!self->check(ec)) return;
                self->buf_.commit_to_internal_input(bytes_transferred);
                self->read_prefix(len);
            });
        }
    }
    else if (len > 8 + var)
    {
        this->check(make_error_code(boost::system::errc::value_too_large));
    }
    else
    {
        std::uintmax_t data_size = 0;

        if(has_options(protocol_options_, protocol_options::use_little_endian))
        {
            if(var)
            {
                for(unsigned i = 0; i < len; ++i)
                    data_size |= static_cast<std::uintmax_t>((*p++) & 0x7fu) << (7 * i);
            }
            else
            {
                for(unsigned i = 0; i < len; ++i)
                    data_size |= static_cast<std::uintmax_t>(*p++) << (8 * i);
            }
        }
        else
        {
            p += len;
            if (var)
            {
                for(unsigned i = 0; i < len; ++i)
                    data_size |= static_cast<std::uintmax_t>((*--p) & 0x7fu) << (7 * i);
            }
            else
            {
                for(unsigned i = 0; i < len; ++i)
                    data_size |= static_cast<std::uintmax_t>(*--p) << (8 * i);
            }
        }

        if (has_options(protocol_options_, protocol_options::include_prefix_in_payload) &&
            ((data_size < len) || (data_size -= len, false)))
        {
            this->check(make_error_code(boost::system::errc::bad_message));
        }
        else if (data_size + len > read_buffer_size_)
        {
            this->check(make_error_code(boost::system::errc::message_size));
        }
        else
        {
            buf_.commit_to_external_input(len);
            if (has_options(protocol_options_, protocol_options::ignore_protocol_bytes))
                buf_.mark_current_external_input();
            read_fixed(data_size);
        }
    }
}


session_manager_ptr session_manager::create()
{
    return std::make_shared<manager_impl>();
}

session_ptr session_manager::create(service_ptr service)
{
    return std::make_shared<session_impl>(std::move(service));
}

session_ptr session_manager::get(unsigned long id)
{
    auto impl = static_cast<manager_impl*>(this);
    std::lock_guard<spinlock> guard(impl->guard);
    auto iter = impl->sessions.find(id);
    if(iter != impl->sessions.end())
        return iter->shared_from_this();
    return session_ptr{};
}

std::size_t session_manager::foreach(object::fn<bool(&)(session*)> f)
{
    std::size_t c = 0;
    auto impl = static_cast<manager_impl*>(this);
    impl->guard.lock();
    auto iter = impl->sessions.begin();
    while (iter != impl->sessions.end())
    {
        auto session = iter->shared_from_this();
        if (!session)
        {
            iter = impl->sessions.upper_bound(iter->id());
        }
        else
        {
            impl->guard.unlock();
            if(!f(session.get())) return c;
            ++c;
            auto id = session->id();
            session = nullptr;
            impl->guard.lock();
            iter = impl->sessions.upper_bound(id);
        }
    }
    impl->guard.unlock();
    return c;
}
