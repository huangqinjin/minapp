#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/service.hpp>

#include <boost/range/iterator_range_core.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>

using namespace minapp;

namespace
{
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

    default:
        std::terminate();
    }
}

void session::read_some(std::size_t bufsize)
{
    socket_.async_read_some(buf_.prepare(bufsize),
        [self = shared_from_this()](const boost::system::error_code& ec,
            std::size_t bytes_transferred)
    {
        if (self->check(ec))
        {
            self->buf_.commit(bytes_transferred);
            self->handler()->read(self.get(), self->buf_);
            self->buf_.consume(self->buf_.size());
            self->read();
        }
    });
}

void session::read_fixed(std::size_t bufsize)
{
    const bool consume = !has_options(protocol_options_, protocol_options::do_not_consume_buffer);
    if (buf_.size() == bufsize)
    {
        handler()->read(this, buf_);
        if (consume) buf_.consume(buf_.size());
        read();
    }
    else if (buf_.size() > bufsize)
    {
        auto n = static_cast<int>(buf_.size() - bufsize);
        buf_.pbump(-n);
        buf_.setg(buf_.eback(), buf_.gptr(), buf_.pptr());
        handler()->read(this, buf_);
        buf_.pbump(n);
        if (consume) buf_.setg(buf_.eback(), buf_.gptr(), buf_.pptr());
        read();
    }
    else
    {
        boost::asio::async_read(socket_,
            buf_.prepare(bufsize - buf_.size()),
            [self = shared_from_this(), consume](const boost::system::error_code& ec,
                std::size_t bytes_transferred)
        {
            if (self->check(ec))
            {
                self->buf_.commit(bytes_transferred);
                self->handler()->read(self.get(), self->buf_);
                if (consume) self->buf_.consume(self->buf_.size());
                self->read();
            }
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

    auto callback = [self = shared_from_this(), delim_length]
            (const boost::system::error_code& ec, std::size_t bytes_transferred)
    {
        if (self->check(ec))
        {
            const bool ignore = has_options(self->protocol_options_, protocol_options::ignore_protocol_bytes);
            if (self->buf_.size() == bytes_transferred)
            {
                if (ignore)
                {
                    auto n = static_cast<int>(delim_length);
                    self->buf_.pbump(-n);
                    self->buf_.setg(self->buf_.eback(), self->buf_.gptr(), self->buf_.pptr());
                }

                self->handler()->read(self.get(), self->buf_);
                self->buf_.consume(self->buf_.size());
            }
            else
            {
                auto n = static_cast<int>(self->buf_.size() - bytes_transferred);
                if (ignore) n += static_cast<int>(delim_length);
                self->buf_.pbump(-n);
                self->buf_.setg(self->buf_.eback(), self->buf_.gptr(), self->buf_.pptr());
                self->handler()->read(self.get(), self->buf_);

                char* p = self->buf_.pptr();
                if (ignore) p += delim_length;
                self->buf_.pbump(n);
                self->buf_.setg(self->buf_.eback(), p, self->buf_.pptr());
            }

            self->read();
        }
    };

    if (delim_length == 1)
    {
        boost::asio::async_read_until(socket_,
           static_cast<boost::asio::streambuf&>(buf_),
           delim[0], std::move(callback));
    }
    else
    {
        boost::asio::async_read_until(socket_,
           static_cast<boost::asio::streambuf&>(buf_),
           delim, std::move(callback));
    }
}

void session::read_prefix(std::size_t len)
{
    if (buf_.size() < len)
    {
        boost::asio::async_read(socket_,
            buf_.prepare(len - buf_.size()),
            [self = shared_from_this(), len](const boost::system::error_code& ec,
                std::size_t bytes_transferred)
        {
            if (self->check(ec))
            {
                self->buf_.commit(bytes_transferred);
                self->read_prefix(len);
            }
        });
    }
    else
    {
        const char* p = buf_.gptr();
        std::uintmax_t data_len = 0;

        if(has_options(protocol_options_, protocol_options::use_little_endian))
        {
            for(int i = 0; i < len; ++i)
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << (8 * i);
        }
        else
        {
            switch (len)
            {
            case 8:
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 56;
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 48;
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 40;
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 32;
            //    [[fallthrough]];
            case 4:
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 24;
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 16;
            //    [[fallthrough]];
            case 2:
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 8;
            //    [[fallthrough]];
            case 1:
                data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p));
                break;
            default:
                std::terminate();
            }
        }

        const bool ignore = has_options(protocol_options_, protocol_options::ignore_protocol_bytes);

        if (data_len > read_buffer_size_)
        {
            // \todo log something
            assert(0);
        }
        else if (buf_.size() == len + data_len)
        {
            if (ignore) buf_.consume(len);
            handler()->read(this, buf_);
            buf_.consume(buf_.size());
            read();
        }
        else if (buf_.size() > len + data_len)
        {
            if (ignore) buf_.consume(len);
            auto n = static_cast<int>(buf_.size() - data_len);
            buf_.pbump(-n);
            buf_.setg(buf_.eback(), buf_.gptr(), buf_.pptr());
            handler()->read(this, buf_);
            buf_.setg(buf_.eback(), buf_.pptr(), buf_.pptr());
            buf_.pbump(n);
            read();
        }
        else
        {
            boost::asio::async_read(socket_,
                buf_.prepare(len + data_len - buf_.size()),
                [self = shared_from_this(), len, ignore](const boost::system::error_code& ec,
                    std::size_t bytes_transferred)
            {
                if (self->check(ec))
                {
                    self->buf_.commit(bytes_transferred);
                    if (ignore) self->buf_.consume(len);
                    self->handler()->read(self.get(), self->buf_);
                    self->buf_.consume(self->buf_.size());
                    self->read();
                }
            });
        }
    }
}
