#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/service.hpp>

#include <boost/range/iterator_range_core.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>

using namespace minapp;

#define CALLBACK(...) [self = shared_from_this(), __VA_ARGS__](const boost::system::error_code& ec, std::size_t bytes_transferred)

namespace
{
    struct dynamic_buffer_adaptor
    {
        triple_buffer& impl;

        using const_buffers_type = triple_buffer::const_buffers_type;
        using mutable_buffers_type = triple_buffer::mutable_buffers_type;

        dynamic_buffer_adaptor(const dynamic_buffer_adaptor&) = delete;
        dynamic_buffer_adaptor(dynamic_buffer_adaptor&&) = default;

        const_buffers_type data() const { return impl.internal_input_buffer(); }
        std::size_t size() const { return impl.internal_input_buffer().size(); }
        std::size_t max_size() const { return impl.max_size() - impl.external_input_buffer().size(); }
        std::size_t capacity() const { return impl.capacity() - impl.external_input_buffer().size(); }
        mutable_buffers_type prepare(std::size_t n) { return impl.prepare_output_buffer(n); }
        void commit(std::size_t n) { impl.commit_to_internal_input(n); }
        void consume(std::size_t n); // [async_]read_util() do not need consume, and it's requirement
                                     // of DynamicBuffer is not compatible with triple_buffer.
                                     // Declare it to meet the DynamicBuffer requirements and no define
                                     // it to catch error if it is called anywhere.
    };

    static_assert(boost::asio::is_dynamic_buffer<dynamic_buffer_adaptor>::value,
                  "dynamic_buffer_adaptor should model the boost::asio::DynamicBuffer concept");


    const std::size_t size_msb = std::size_t(-1) - (std::size_t(-1) >> 1u);

    const std::string crlf = { '\r', '\n' };

    std::atomic_ulong id = ATOMIC_VAR_INIT(1);
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
    close();
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

void session::close()
{
    if (this->status_ >= status::closing) return;
    this->status_ = status::closing;
    boost::system::error_code ignored;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
    this->status_ = status::closed;
    handler()->close(this);
}

bool session::check(const boost::system::error_code& ec)
{
    //if (this->status_ >= status::closing) return false;
    if (ec)
    {
        handler()->error(this, ec);
        close();
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
        if (ec)
        {
            self->handler()->error(self.get(), ec);
            boost::system::error_code ignored;
            self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
            self->socket_.close(ignored);
            promise.set_exception(std::make_exception_ptr(boost::system::system_error(ec)));
        }
        else
        {
            self->connect();
            promise.set_value(std::move(self));
        }
    });

    return future;
}

void session::connect()
{
    status_ = status::connected;
    handler()->connect(this, socket_.remote_endpoint());
    read();
}

void session::write()
{
    if (this->status_ >= status::closing) return;
    auto marker = write_queue_.mark();
    if (marker <= 0) return;

    boost::asio::async_write(socket_, boost::make_iterator_range(write_queue_.marked()),
        [self = shared_from_this(), marker](const boost::system::error_code& ec,
            std::size_t bytes_transferred)
    {
        if (self->check(ec))
        {
            self->handler()->write(self.get(), self->write_queue_.marked());
            self->write_queue_.clear_marked();
            self->write();
        }
    });
}

void session::read()
{
    if (this->status_ >= status::closing) return;
    this->status_ = status::reading;
    switch (protocol_)
    {
    case protocol::none:
        this->status_ = status::connected;
        break;

    case protocol::any:
        read_some((std::min<std::size_t>)(read_buffer_size_, 65536));
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
    const bool consume = !has_options(protocol_options_, protocol_options::do_not_consume_buffer);
    if(buf_.internal_input_buffer().size() > 0)
    {
        buf_.commit_to_external_input(bufsize);
        handler()->read(this, buf_);
        if(consume) buf_.consume_whole_external_input();
        read();
    }
    else
    {
        socket_.async_read_some(buf_.prepare_output_buffer(bufsize), CALLBACK(consume)
        {
            if (!self->check(ec)) return;
            self->buf_.commit_to_internal_input(bytes_transferred);
            self->buf_.commit_whole_internal_input();
            self->handler()->read(self.get(), self->buf_);
            if(consume) self->buf_.consume(self->buf_.size());
            self->read();
        });
    }
}

void session::read_fixed(std::size_t bufsize)
{
    const bool consume = !has_options(protocol_options_, protocol_options::do_not_consume_buffer);
    const std::size_t remaining = buf_.internal_input_buffer().size();
    if (remaining >= bufsize)
    {
        buf_.commit_to_external_input(bufsize);
        handler()->read(this, buf_);
        if(consume) buf_.consume_whole_external_input();
        read();
    }
    else
    {
        boost::asio::async_read(socket_, buf_.prepare_output_buffer(bufsize - remaining), CALLBACK(consume)
        {
            if (!self->check(ec)) return;
            self->buf_.commit_to_internal_input(bytes_transferred);
            self->buf_.commit_whole_internal_input();
            self->handler()->read(self.get(), self->buf_);
            if (consume) self->buf_.consume_whole_external_input();
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
    if (delim_length == 0) return read_some((std::min<std::size_t>)(read_buffer_size_, 65536));

    const bool consume = !has_options(protocol_options_, protocol_options::do_not_consume_buffer);
    const bool ignore = has_options(protocol_options_, protocol_options::ignore_protocol_bytes);
    auto callback = CALLBACK(delim_length, consume, ignore)
    {
        if (!self->check(ec)) return;

        // buf has already been committed and bytes_transferred is the length from the beginning of
        // internal input to the delimiter (including), so it may be smaller than the size of internal input.
        self->buf_.commit_to_external_input(bytes_transferred - delim_length * ignore);
        self->handler()->read(self.get(), self->buf_);
        self->buf_.commit_to_external_input(delim_length * ignore);
        if(consume) self->buf_.consume_whole_external_input();
        self->read();
    };

    if (delim_length == 1)
    {
        boost::asio::async_read_until(socket_, dynamic_buffer_adaptor{buf_}, delim[0], std::move(callback));
    }
    else
    {
        boost::asio::async_read_until(socket_, dynamic_buffer_adaptor{buf_}, delim, std::move(callback));
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
        std::size_t sz = len - remaining;
        if (var) len += size_msb;
        boost::asio::async_read(socket_, buf_.prepare_output_buffer(sz), CALLBACK(len)
        {
            if (!self->check(ec)) return;
            self->buf_.commit_to_internal_input(bytes_transferred);
            self->read_prefix(len);
        });
    }
    else
    {
        std::uintmax_t data_len = 0;

        if(has_options(protocol_options_, protocol_options::use_little_endian))
        {
            if(var)
            {
                for(unsigned i = 0; i < len; ++i)
                    data_len |= static_cast<std::uintmax_t>((*p++) & 0x7fu) << (7 * i);
            }
            else
            {
                for(unsigned i = 0; i < len; ++i)
                    data_len |= static_cast<std::uintmax_t>(*p++) << (8 * i);
            }

        }
        else
        {
            p += len;
            if (var)
            {
                for(unsigned i = 0; i < len; ++i)
                    data_len |= static_cast<std::uintmax_t>((*--p) & 0x7fu) << (7 * i);
            }
            else
            {
                for(unsigned i = 0; i < len; ++i)
                    data_len |= static_cast<std::uintmax_t>(*--p) << (8 * i);
            }
        }

        const bool ignore = has_options(protocol_options_, protocol_options::ignore_protocol_bytes);

        if (data_len > read_buffer_size_)
        {
            this->check(make_error_code(boost::system::errc::message_size));
        }
        else
        {
            buf_.commit_to_external_input(len);
            /// \todo too dangerous to consume all, consider more
            if (ignore) buf_.consume_whole_external_input();
            read_fixed(data_len);
        }
    }
}
