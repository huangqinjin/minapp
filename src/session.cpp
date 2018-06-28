#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/service.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <atomic>

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

}

unsigned long session::id() const
{
    return id_;
}

boost::asio::streambuf& session::streambuf()
{
    return streambuf_;
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

std::future<session_ptr> session::connect(const endpoint& endpoint)
{
    //C++14 but an asio handler must meet the requirements of CopyConstructible types.
    /*std::promise<session::ptr> promise;
    std::future<session::ptr> future = promise.get_future();
    auto self = shared_from_this();
    socket_.async_connect(endpoint,
    [self, promise = std::move(promise)](const boost::system::error_code& ec) mutable
    {
    if (ec)
    {
    self->handler()->error(self, ec);
    boost::system::error_code ignored;
    self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    self->socket_.close(ignored);
    promise.set_exception(std::make_exception_ptr(boost::system::system_error(ec)));
    }
    else
    {
    self->connect(self);
    promise.set_value(std::move(self));
    }
    });*/
    auto self = shared_from_this();
    auto p = std::make_shared<std::promise<session_ptr>>();
    auto future = p->get_future();
    socket_.async_connect(endpoint,
        [self, p](const boost::system::error_code& ec)
    {
        if (ec)
        {
            self->handler()->error(self.get(), ec);
            boost::system::error_code ignored;
            self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
            self->socket_.close(ignored);
            self->status_ = status::closed;
            p->set_exception(std::make_exception_ptr(boost::system::system_error(ec)));
        }
        else
        {
            self->connect();
            p->set_value(std::move(self));
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

    auto self = shared_from_this();
    boost::asio::async_write(socket_, boost::make_iterator_range(write_queue_.marked()),
        [self, marker](const boost::system::error_code& ec,
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
    auto self = shared_from_this();
    socket_.async_read_some(streambuf_.prepare(bufsize),
        [self](const boost::system::error_code& ec,
            std::size_t bytes_transferred)
    {
        if (self->check(ec))
        {
            self->streambuf_.commit(bytes_transferred);
            self->handler()->read(self.get(), self->streambuf_);
            self->streambuf_.consume(self->streambuf_.size());
            self->read();
        }
    });
}

void session::read_fixed(std::size_t bufsize)
{
    const bool consume = !has_options(protocol_options_, protocol_options::do_not_consume_buffer);
    if (streambuf_.size() == bufsize)
    {
        handler()->read(this, streambuf_);
        if (consume) streambuf_.consume(streambuf_.size());
        read();
    }
    else if (streambuf_.size() > bufsize)
    {
        int n = static_cast<int>(streambuf_.size() - bufsize);
        streambuf_.pbump(-n);
        streambuf_.setg(streambuf_.eback(), streambuf_.gptr(), streambuf_.pptr());
        handler()->read(this, streambuf_);
        streambuf_.pbump(n);
        if (consume) streambuf_.setg(streambuf_.eback(), streambuf_.gptr(), streambuf_.pptr());
        read();
    }
    else
    {
        auto self = shared_from_this();
        boost::asio::async_read(socket_,
            streambuf_.prepare(bufsize - streambuf_.size()),
            [self, consume](const boost::system::error_code& ec,
                std::size_t bytes_transferred)
        {
            if (self->check(ec))
            {
                self->streambuf_.commit(bytes_transferred);
                self->handler()->read(self.get(), self->streambuf_);
                if (consume) self->streambuf_.consume(self->streambuf_.size());
                self->read();
            }
        });
    }
}

void session::read_delim(char delim)
{
    auto self = shared_from_this();
    boost::asio::async_read_until(socket_,
        static_cast<boost::asio::streambuf&>(streambuf_), delim,
        [self](const boost::system::error_code& ec,
            std::size_t bytes_transferred)
    {
        if (self->check(ec))
        {
            const bool ignore = has_options(self->protocol_options_, protocol_options::ignore_protocol_bytes);
            if (self->streambuf_.size() == bytes_transferred)
            {
                if (ignore)
                {
                    int n = 1;
                    self->streambuf_.pbump(-n);
                    self->streambuf_.setg(self->streambuf_.eback(), self->streambuf_.gptr(), self->streambuf_.pptr());
                }

                self->handler()->read(self.get(), self->streambuf_);
                self->streambuf_.consume(self->streambuf_.size());
            }
            else
            {
                int n = static_cast<int>(self->streambuf_.size() - bytes_transferred);
                if (ignore) n += 1;
                self->streambuf_.pbump(-n);
                self->streambuf_.setg(self->streambuf_.eback(), self->streambuf_.gptr(), self->streambuf_.pptr());
                self->handler()->read(self.get(), self->streambuf_);

                char* p = self->streambuf_.pptr();
                if (ignore) p += 1;
                self->streambuf_.pbump(n);
                self->streambuf_.setg(self->streambuf_.eback(), p, self->streambuf_.pptr());
            }

            self->read();
        }
    });
}

void session::read_delim(const std::string& delim)
{
    std::size_t delim_length = delim.size();
    if (delim_length == 0) return read_some((std::min<std::size_t>)(read_buffer_size_, 65536));
    if (delim_length == 1) return read_delim(delim[0]);

    auto self = shared_from_this();
    boost::asio::async_read_until(socket_,
        static_cast<boost::asio::streambuf&>(streambuf_), delim,
        [self, delim_length](const boost::system::error_code& ec,
            std::size_t bytes_transferred)
    {
        if (self->check(ec))
        {
            const bool ignore = has_options(self->protocol_options_, protocol_options::ignore_protocol_bytes);
            if (self->streambuf_.size() == bytes_transferred)
            {
                if (ignore)
                {
                    int n = static_cast<int>(delim_length);
                    self->streambuf_.pbump(-n);
                    self->streambuf_.setg(self->streambuf_.eback(), self->streambuf_.gptr(), self->streambuf_.pptr());
                }

                self->handler()->read(self.get(), self->streambuf_);
                self->streambuf_.consume(self->streambuf_.size());
            }
            else
            {
                int n = static_cast<int>(self->streambuf_.size() - bytes_transferred);
                if (ignore) n += static_cast<int>(delim_length);
                self->streambuf_.pbump(-n);
                self->streambuf_.setg(self->streambuf_.eback(), self->streambuf_.gptr(), self->streambuf_.pptr());
                self->handler()->read(self.get(), self->streambuf_);

                char* p = self->streambuf_.pptr();
                if (ignore) p += delim_length;
                self->streambuf_.pbump(n);
                self->streambuf_.setg(self->streambuf_.eback(), p, self->streambuf_.pptr());
            }

            self->read();
        }
    });
}

void session::read_prefix(std::size_t len)
{
    if (streambuf_.size() < len)
    {
        auto self = shared_from_this();
        boost::asio::async_read(socket_,
            streambuf_.prepare(len - streambuf_.size()),
            [self, len](const boost::system::error_code& ec,
                std::size_t bytes_transferred)
        {
            if (self->check(ec))
            {
                self->streambuf_.commit(bytes_transferred);
                self->read_prefix(len);
            }
        });
    }
    else
    {
        const char* p = streambuf_.gptr();
        std::uintmax_t data_len = 0;

        switch (len)
        {
        case 8:
            data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 56;
            data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 48;
            data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 40;
            data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 32;
            [[fallthrough]];
        case 4:
            data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 24;
            data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 16;
            [[fallthrough]];
        case 2:
            data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p++)) << 8;
            [[fallthrough]];
        case 1:
            data_len |= static_cast<std::uintmax_t>(static_cast<unsigned char>(*p));
            break;
        default:
            std::terminate();
        }

        const bool ignore = has_options(protocol_options_, protocol_options::ignore_protocol_bytes);

        if (data_len > read_buffer_size_)
        {
            // \todo log something
            assert(0);
        }
        else if (streambuf_.size() == len + data_len)
        {
            if (ignore) streambuf_.consume(len);
            handler()->read(this, streambuf_);
            streambuf_.consume(streambuf_.size());
            read();
        }
        else if (streambuf_.size() > len + data_len)
        {
            if (ignore) streambuf_.consume(len);
            auto n = static_cast<int>(streambuf_.size() - data_len);
            streambuf_.pbump(-n);
            streambuf_.setg(streambuf_.eback(), streambuf_.gptr(), streambuf_.pptr());
            handler()->read(this, streambuf_);
            streambuf_.setg(streambuf_.eback(), streambuf_.pptr(), streambuf_.pptr());
            streambuf_.pbump(n);
            read();
        }
        else
        {
            auto self = shared_from_this();
            boost::asio::async_read(socket_,
                streambuf_.prepare(len + data_len - streambuf_.size()),
                [self, len, ignore](const boost::system::error_code& ec,
                    std::size_t bytes_transferred)
            {
                if (self->check(ec))
                {
                    self->streambuf_.commit(bytes_transferred);
                    if (ignore) self->streambuf_.consume(len);
                    self->handler()->read(self.get(), self->streambuf_);
                    self->streambuf_.consume(self->streambuf_.size());
                    self->read();
                }
            });
        }
    }
}
