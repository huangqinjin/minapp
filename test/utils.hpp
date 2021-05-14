#ifndef MINAPP_TEST_UTILS_HPP
#define MINAPP_TEST_UTILS_HPP

#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/hexdump.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>

#include <boost/asio/ip/tcp.hpp>
#include <boost/exception/diagnostic_information.hpp>

#undef ERROR
#define LOG(TAG) logging::stream().base() << __FILE__ << ':' << __LINE__ << " [" << #TAG << "] - "
#define NSLOG(TAG) LOG(TAG) << '[' << name() << ':' << session->id() << "] "

using namespace minapp;

class logging final : public noexcept_handler_impl
{
    void connect_impl(session* session, const endpoint& ep) override
    {
        if (h->log_connect()) switch (ep.protocol().family())
        {
            case BOOST_ASIO_OS_DEF(AF_INET):
            case BOOST_ASIO_OS_DEF(AF_INET6):
                NSLOG(CONN) << "connect to " << reinterpret_cast<boost::asio::ip::tcp::endpoint const&>(ep);
                break;
            default:
                NSLOG(CONN) << "connect to unknown protocol endpoint";
                break;
        }
        h->connect(session, ep);
    }

    void read_impl(session* session, buffer& buf) override
    {
        if (h->log_read())
        {
            std::size_t size = buf.size();
            const void* p = buf.data();
            hexdump{NSLOG(READ) << "bufsize = " << size << '\n'}(p, size);
        }
        h->read(session, buf);
    }

    void write_impl(session* session, persistent_buffer_list& list) override
    {
        if (h->log_write()) for(auto& buf : list)
        {
            std::size_t size = buf.size();
            const void* p = buf.data();
            hexdump{NSLOG(WRITE) << "bufsize = " << size << '\n'}(p, size);
        }
        h->write(session, list);
    }

    void except_impl(session* session, std::exception& e) override
    {
        if (h->log_except())
        {
            NSLOG(EXCEPT) << "status:" << (int)session->status() << '\n' << boost::diagnostic_information(e);
        }
        h->except(session, e);
    }

    void error_impl(session* session, boost::system::error_code ec) override
    {
        if (h->log_error())
        {
            NSLOG(ERROR) << "status:" << (int)session->status() << ' ' << ec << ' ' << '-' << ' ' << ec.message();
        }
        h->error(session, ec);
    }

    void close_impl(session* session) override
    {
        if (h->log_close())
        {
            NSLOG(CLOSE);
        }
        h->close(session);
    }

public:
    class handler : public minapp::handler
    {
#define LOGFLAG(x) \
    private: bool log_ ## x ## _ = true; \
    public: bool log_ ## x () const noexcept { return log_ ## x ## _; } \
    public: void log_ ## x (bool en) noexcept { log_ ## x ## _ = en; }

    LOGFLAG(connect)
    LOGFLAG(read)
    LOGFLAG(write)
    LOGFLAG(except)
    LOGFLAG(error)
    LOGFLAG(close)

#undef LOGFLAG

    private:
        const std::string name_;

    public:
        explicit handler(std::string name) : name_(std::move(name)) {}
        const std::string& name() const noexcept { return name_; }
    };

    using named_handler_ptr = std::shared_ptr<handler>;

    named_handler_ptr const h;

    explicit logging(named_handler_ptr h) : h(std::move(h)) {}

    template<typename Handler, typename... Args>
    static handler_ptr wrap(Args&&... args)
    {
        return std::make_shared<logging>(std::make_shared<Handler>(std::forward<Args>(args)...));
    }

    const std::string& name() const noexcept
    {
        return h->name();
    }

    handler_ptr wrapped() noexcept override
    {
        return h;
    }

    struct stream : std::ostringstream
    {
        std::ostream& base() noexcept
        {
            return *this;
        }

        ~stream() override
        {
            static std::mutex guard;
            std::lock_guard _(guard);
            std::cout << str() << std::endl;
        }
    };
};

#endif
