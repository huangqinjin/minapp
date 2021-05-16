#ifndef MINAPP_TEST_UTILS_HPP
#define MINAPP_TEST_UTILS_HPP

#include <minapp/connector.hpp>
#include <minapp/acceptor.hpp>
#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/hexdump.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <regex>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
#include <boost/asio/local/stream_protocol.hpp>
#endif
#if BOOST_ASIO_HAS_BLUETOOTH_SOCKETS
#include <boost/asio/bluetooth/stream_protocol.hpp>
#endif
#include <boost/exception/diagnostic_information.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>

#undef ERROR
#define LOG(TAG) logging::stream().base() << __FILE__ << ':' << __LINE__ << " [" << #TAG << "] - "
#define NSLOG(TAG) LOG(TAG) << '[' << name() << ':' << session->id() << "] "


inline minapp::endpoint make_endpoint(std::string s)
{
    boost::algorithm::trim(s);
    std::smatch matches;

#if BOOST_ASIO_HAS_LOCAL_SOCKETS
    if (boost::algorithm::ends_with(s, ".sock"))
    {
        using namespace boost::asio::local;
        if (s[0] == ':') s[0] = '\0';
        return stream_protocol::endpoint(s);
    }
#endif
#if BOOST_ASIO_HAS_BLUETOOTH_SOCKETS
    if (std::regex_match(s, matches, std::regex(R"BT(\[((?:(?:|:|-)[[:xdigit:]]{2}){6})\]:(\d{1,2}))BT")))
    {
        using namespace boost::asio::bluetooth;
        return stream_protocol::endpoint(make_address(matches[1]), std::stoi(matches[2]));
    }
#endif
    if (std::regex_match(s, matches, std::regex(R"V4(((?:(?:^|\.)\d{1,3}){4}):(\d{1,6}))V4")))
    {
        using namespace boost::asio::ip;
        return tcp::endpoint(make_address_v4(matches[1]), std::stoi(matches[2]));
    }
    if (std::regex_match(s, matches, std::regex(R"V6(\[([:[:xdigit:]]+)\]:(\d{1,6}))V6")))
    {
        using namespace boost::asio::ip;
        return tcp::endpoint(make_address_v6(matches[1]), std::stoi(matches[2]));
    }
    return {};
}

inline minapp::endpoint make_endpoint(bool server, const char* protocol, int port = -1)
{
    if (std::strcmp(protocol, "ipv4") == 0)
    {
        using namespace boost::asio::ip;
        if (port < 0) port = 2333;
        if (server) return tcp::endpoint(tcp::v4(), port);
        else return tcp::endpoint(address_v4::loopback(), port);
    }
    if (std::strcmp(protocol, "ipv6") == 0)
    {
        using namespace boost::asio::ip;
        if (port < 0) port = 2333;
        if (server) return tcp::endpoint(tcp::v6(), port);
        else return tcp::endpoint(address_v6::loopback(), port);
    }
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
    if (std::strcmp(protocol, "local") == 0 || std::strcmp(protocol, "alocal") == 0)
    {
        using namespace boost::asio::local;
        bool abstract = protocol[0] == 'a';
        if (port < 0) port = 0;
        char buf[64] = { '\0' };
        port = std::snprintf(buf + 1, sizeof(buf) - 1, "/tmp/minapp.%d.sock", port);
        return stream_protocol::endpoint(boost::asio::string_view(buf + !abstract, port + abstract + 1));
    }
#endif
#if BOOST_ASIO_HAS_BLUETOOTH_SOCKETS
    if (std::strcmp(protocol, "bluetooth") == 0)
    {
        using namespace boost::asio::bluetooth;
        if (port < 0) port = 30;
        return stream_protocol::endpoint(address(), port);
    }
#endif
    return {};
}

inline minapp::endpoint make_endpoint(bool server, const char* protocol, const char* s)
{
    minapp::endpoint ep;
    if (s) ep = make_endpoint(s);
    else if (protocol) ep = make_endpoint(server, protocol);
    if (ep == minapp::endpoint() && protocol && s && (s[0] == ':' ? ++s : s)[0] != '\0')
    {
        char* end;
        long port = std::strtol(s, &end, 10);
        if (*end == '\0') ep = make_endpoint(server, protocol, (int)port);
    }
    return ep;
}

inline std::pair<minapp::endpoint, minapp::endpoint> make_endpoint_pair(
        const char* protocol, const char* server, const char* client)
{
    auto ep = make_endpoint(false, protocol, client);
    if (protocol == nullptr) switch (ep.protocol().family())
    {
        case BOOST_ASIO_OS_DEF(AF_INET):
            protocol = "ipv4";
            break;
        case BOOST_ASIO_OS_DEF(AF_INET6):
            protocol = "ipv6";
            break;
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
        case AF_LOCAL:
            protocol = client[0] == ':' ? "alocal" : "local";
            break;
#endif
#if BOOST_ASIO_HAS_BLUETOOTH_SOCKETS
        case BOOST_ASIO_OS_DEF(AF_BLUETOOTH):
            protocol = "bluetooth";
            break;
#endif
    }
    return std::make_pair(make_endpoint(true, protocol, server), ep);
}

inline std::string to_string(const minapp::endpoint& ep)
{
    switch (ep.protocol().family())
    {
        case BOOST_ASIO_OS_DEF(AF_UNSPEC):
            return "null";
        case BOOST_ASIO_OS_DEF(AF_INET):
        case BOOST_ASIO_OS_DEF(AF_INET6):
            {
                std::ostringstream os;
                os << reinterpret_cast<boost::asio::ip::tcp::endpoint const&>(ep);
                return os.str();
            }
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
        case AF_UNIX:
            {
                const char* s = reinterpret_cast<boost::asio::local::stream_protocol::endpoint const&>(ep).data()->sa_data;
                if (s[0] == '\0') return ':' + std::string(s + 1);
                else return s;
            }
#endif
#if BOOST_ASIO_HAS_BLUETOOTH_SOCKETS
        case BOOST_ASIO_OS_DEF(AF_BLUETOOTH):
            {
                std::ostringstream os;
                os << reinterpret_cast<boost::asio::bluetooth::stream_protocol::endpoint const&>(ep);
                return os.str();
            }
#endif
        default:
            char buf[40];
            std::snprintf(buf, sizeof(buf), "unknown(AF:%d)", ep.protocol().family());
            return buf;
    }
}


using namespace minapp;

class logging final : public noexcept_handler_impl
{
    void connect_impl(session* session, const endpoint& ep) override
    {
        if (h->log_connect())
        {
            NSLOG(CONN) << "connect to " << to_string(
#if BOOST_ASIO_HAS_LOCAL_SOCKETS
                // https://stackoverflow.com/questions/17090043/unix-domain-sockets-accept-not-setting-sun-path
                (ep.protocol().family() == AF_UNIX && ep.size() <= 2) ? session->socket().local_endpoint() :
#endif
                    ep);
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

struct workers
{
    std::vector<service_ptr> services;
    std::vector<std::thread> threads;
    workers(std::vector<service_ptr> services, std::size_t threads)
            : services(std::move(services))
    {
        this->threads.reserve(this->services.size() * threads);
        for (const auto& service : this->services)
        {
            for (std::size_t i = 0; i < threads; ++i)
                this->threads.emplace_back(&context::run, service->context());
        }
    }
    ~workers() noexcept(false)
    {
        for (const auto& service : this->services)
            service->context()->stop();
        for (auto& th : this->threads)
            th.join();
    }
};

#endif
