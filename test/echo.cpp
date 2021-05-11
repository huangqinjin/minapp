#include <minapp/connector.hpp>
#include <minapp/acceptor.hpp>
#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/hexdump.hpp>

#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/endian/buffers.hpp>
#include <boost/crc.hpp>

std::mutex ostream_lock;
struct ostream_guard
{
    ostream_guard() { ostream_lock.lock(); }
    ~ostream_guard() { stream() << std::endl; ostream_lock.unlock(); }
    std::ostream& stream() { return std::cout; }
};

#undef ERROR
#define LOG(TAG) ostream_guard{}.stream() << __FILE__ << ':' << __LINE__ << " [" << #TAG << "] - "
#define NSLOG(TAG) LOG(TAG) << '[' << name() << ':' << session->id() << "] "


using namespace minapp;

class logging_handler final : public noexcept_handler_impl
{
public:
    class named : public handler
    {
    public:
        virtual std::string name() const = 0;
    };

    using named_handler_ptr = std::shared_ptr<named>;

    named_handler_ptr const h;

    explicit logging_handler(named_handler_ptr h) : h(std::move(h)) {}

    static handler_ptr wrap(named_handler_ptr h)
    {
        return std::make_shared<logging_handler>(std::move(h));
    }

    template<typename Handler>
    static handler_ptr wrap()
    {
        return wrap(std::make_shared<Handler>());
    }

    std::string name() const
    {
        return h->name();
    }

    handler_ptr wrapped() noexcept override
    {
        return h;
    }

    void connect_impl(session* session, const endpoint& ep) override
    {
        switch (ep.protocol().family())
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
        std::size_t size = buf.size();
        const void* p = buf.data();
        hexdump{NSLOG(READ) << "bufsize = " << size << '\n'}(p, size);
        h->read(session, buf);
    }

    void write_impl(session* session, persistent_buffer_list& list) override
    {
        for(auto& buf : list)
        {
            std::size_t size = buf.size();
            const void* p = buf.data();
            hexdump{NSLOG(WRITE) << "bufsize = " << size << '\n'}(p, size);
        }
        h->write(session, list);
    }

    void except_impl(session* session, std::exception& e) override
    {
        NSLOG(EXCEPT) << "status:" << (int)session->status() << '\n' << boost::diagnostic_information(e);
        h->except(session, e);
    }

    void error_impl(session* session, boost::system::error_code ec) override
    {
        NSLOG(ERROR) << "status:" << (int)session->status() << ' ' << ec << ' ' << '-' << ' ' << ec.message();
        h->error(session, ec);
    }

    void close_impl(session* session) override
    {
        NSLOG(CLOSE);
        h->close(session);
    }
};

/**
 *      C --> S                    S --> C
 *   ------------               ------------
 *   |  protocol |              |    len    |
 *   ------------               ------------
 *   |  options  |              |    type   |
 *   ------------               ------------
 *   |  body crc |              |  body crc |
 *   ------------               ------------
 *   |   body    |              |   body    |
 *   ------------               ------------
 */
struct header
{
    union
    {
        boost::endian::little_uint32_buf_t len;
        boost::endian::big_int32_buf_t protocol;
    };

    union
    {
        boost::endian::big_int32_buf_t type;
        boost::endian::little_uint32_buf_t options;
    };

    boost::endian::little_uint32_buf_t crc;

    static header make(std::size_t len, int type, std::uint32_t crc)
    {
        header h;
        h.len = static_cast<unsigned>(len + sizeof(h) - sizeof(h.len));
        h.type = type;
        h.crc = crc;
        return h;
    }

    static header make(enum protocol p, enum protocol_options o, std::uint32_t crc)
    {
        header h;
        h.protocol = static_cast<int>(p);
        h.options = static_cast<unsigned>(o);
        h.crc = crc;
        return h;
    }

    static const header& parse(boost::asio::const_buffer buf)
    {
        static_assert(alignof(header) == 1);
        if(buf.size() < sizeof(header))
            throw std::logic_error("bad length header");
        return *static_cast<const header*>(buf.data());
    }

    static header& parse(boost::asio::mutable_buffer buf)
    {
        static_assert(alignof(header) == 1);
        if(buf.size() < sizeof(header))
            throw std::logic_error("bad length header");
        return *static_cast<header*>(buf.data());
    }
};


class ServerHandler : public logging_handler::named
{
    std::string name() const override
    {
        return "server";
    }

    void except(session* session, std::exception& e) override
    {
        session->close(true);
    }

    void connect(session* session, const endpoint& ep) override
    {
        session->protocol(protocol::delim_crlf, 32);
    }

    void read(session* session, buffer& buf) override
    {
#include <boost/asio/yield.hpp>
        reenter(session)
        {
            NSLOG(GREET) << std::string_view((const char*)buf.data(), buf.size());

        read_new_packet:

            yield {
                session->protocol(protocol::fixed, protocol_options::do_not_consume_buffer);
                session->read_buffer_size(sizeof(header));
            }

            yield {
                session->read_buffer_size(65536);
                const header& h = header::parse(buf);
                std::uint32_t options = h.options.value();
                switch(auto p = protocol{h.protocol.value()})
                {
                case protocol::fixed:
                    session->protocol(p, protocol_options::do_not_consume_buffer);
                    session->read_buffer_size(options);
                    break;

                case protocol::delim:
                    session->protocol(p, protocol_options::do_not_consume_buffer);
                    session->delimiter(std::string((const char*)h.options.data(), sizeof(h.options)));
                    boost::endian::native_to_big_inplace(options);
                    break;

                default:
                    session->protocol(p, protocol_options{options} | protocol_options::do_not_consume_buffer);
                }

                session->attrs.set("CRC", (std::uint32_t)h.crc.value());
                session->attrs.set("OPT", options);
            }

            {
                boost::crc_32_type crc32;
                crc32.process_bytes(buf.data(), buf.size());
                if(session->attrs.get<std::uint32_t>("CRC") != crc32.checksum())
                {
                    std::ostringstream os;
                    os << "CRC NOT match for protocol "
                       << static_cast<int>(session->protocol())
                       << " and protocol_options " << std::hex
                       << session->attrs.get<std::uint32_t>("OPT");
                    throw std::logic_error(os.str());
                }

                auto body = buf.whole();
                crc32.reset();
                crc32.process_bytes(body.data(), body.size());

                auto seq = ++session->servlet<int>("SEQ");
                auto header = header::make(body.size(), seq, crc32.checksum());
                session->write(header, body);
            }

            goto read_new_packet;
        }
#include <boost/asio/unyield.hpp>
    }
};

class ClientHandler : public logging_handler::named
{
    std::string name() const override
    {
        return "client";
    }

    void connect(session* session, const endpoint& ep) override
    {
        session->protocol(protocol::prefix_32, protocol_options::use_little_endian);
    }

    void except(session* session, std::exception& e) override
    {
        session->close(true);
    }

    void read(session* session, buffer& buf) override
    {
        auto& h = header::parse(buf);
        buf += sizeof(header);
        boost::crc_32_type crc32;
        crc32.process_bytes(buf.data(), buf.size());
        if(h.crc.value() != crc32.checksum())
        {
            std::ostringstream os;
            os << "CRC NOT match for server packet " << h.type.value();
            throw std::logic_error(os.str());
        }
        else
        {
            NSLOG(CHECK) << "CRC match for server packet " << h.type.value();
        }
    }
};


int main(int argc, char* argv[]) try
{
    auto server = minapp::acceptor::create(logging_handler::wrap<ServerHandler>());
    auto client = minapp::connector::create(logging_handler::wrap<ClientHandler>());

    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::tcp::v4(), 2333);
    server->bind(ep);

    std::thread([client] {
        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address_v4::loopback(), 2333);
        auto future = client->connect(ep);
        auto session = future.get();

        boost::crc_32_type crc32;
        for(auto p = protocol::delim_crlf; p != protocol::none; crc32.reset(),
            std::this_thread::sleep_for(std::chrono::seconds(1))) switch (p)
        {
            case protocol::delim_crlf: {
                using namespace std::string_view_literals;
                auto msg = persist("greet from client!\r\n"sv);  // std::string_view
                assert(msg.storage().type() == object::type_id<void>());
                session->write(msg);
                p = protocol::fixed;
                break;
            }

            case protocol::fixed: {
                auto body = persist(std::vector<char>{'f','i','x','e','d'});  // std::vector
                assert(body.storage().type() == object::type_id<std::vector<char>>());
                crc32.process_bytes(body.data(), body.size());
                auto header = header::make(p, protocol_options{5}, crc32.checksum());
                session->write(header, body);
                p = protocol::delim;
                break;
            }

            case protocol::delim: {
                static std::array<const char32_t, 6> buf{U"delim"};
                auto msg = persist(buf);  // std::array<const char32_t, 6>  (read-only container)
                assert(msg.storage().type() == object::type_id<void>());
                std::uint32_t num = 0x12345678;
                boost::endian::native_to_big_inplace(num);
                auto delim = persist(num);                    // POD
                assert(delim.storage().type() == object::type_id<std::uint32_t>());
                crc32.process_bytes(msg.data(), msg.size());
                crc32.process_bytes(delim.data(), delim.size());
                auto header = header::make(p, protocol_options{num}, crc32.checksum());
                session->write(header, msg, delim);
                p = protocol::delim_zero;
                break;
            }

            case protocol::delim_zero: {
                auto body = persist("delim_zero");  // char[11]
                assert(body.storage().type() == object::type_id<char[11]>());
                crc32.process_bytes(body.data(), body.size() - 1);
                auto header = header::make(p, protocol_options::ignore_protocol_bytes, crc32.checksum());
                session->write(header, body);
                p = protocol::prefix_8;
                break;
            }

            case protocol::prefix_8: {
                const char* s = "\x19prefix_8 include prefix";
                auto body = persist(boost::asio::buffer(s, s[0]));  // const_buffer
                assert(body.storage().type() == object::type_id<void>());
                crc32.process_bytes(body.data(), body.size());
                auto header = header::make(p, protocol_options::include_prefix_in_payload, crc32.checksum());
                session->write(header, body);
                p = protocol::prefix_16;
                break;
            }

            case protocol::prefix_16: {
                const char16_t buf[] = u"\u1200prefix_16";
                auto body = persist(&buf[0], std::size(buf) - 1);  // (const char16_t*, n)
                assert(body.storage().type() == object::type_id<char16_t[]>());
                crc32.process_bytes(body.data(), body.size());
                auto header = header::make(p, {}, crc32.checksum());
                session->write(header, body);
                p = protocol::prefix_64;
                break;
            }

            case protocol::prefix_64: {
                char s[] = "prefix_64 little endian";
                auto msg = persist(boost::asio::buffer(s));  // mutable_buffer
                assert(msg.storage().type() == object::type_id<std::byte[]>());
                auto prefix = persist(std::array<char, 8>{sizeof(s)});  // std::array<char, 8>
                assert(prefix.storage().type() == (object::type_id<std::array<char, 8>>()));
                auto list = make_list(prefix, msg);
                for(auto&& buf : list) crc32.process_bytes(buf.data(), buf.size());
                auto header = persist(header::make(p, protocol_options::use_little_endian, crc32.checksum()));  // POD
                assert(header.storage().type() == object::type_id<struct header>());                
                list.push_front(header);
                session->write(list);
                p = protocol::prefix_var;
                break;
            }

            case protocol::prefix_var: {
                std::string s = "\x82\x01prefix_var little endian ";
                s.resize(130 + 2, 'x');
                s.back() = 'o';
                auto body = persist(std::move(s));  // std::string
                assert(body.storage().type() == object::type_id<std::string>());
                assert(s.empty());
                crc32.process_bytes(body.data(), body.size());
                auto header = header::make(p, protocol_options::use_little_endian, crc32.checksum());
                session->write(header, body);
                p = protocol::any;
                break;
            }

            case protocol::any: {
                std::string s = "\x81\x02prefix_var ";
                s.resize(130 + 2, 'z');
                s.back() = 'o';
                auto body = persist<std::string>(object(std::move(s)));  // object
                assert(body.storage().type() == object::type_id<std::string>());
                assert(s.empty());
                crc32.process_bytes(body.data(), body.size());
                auto header = header::make(protocol::prefix_var, {}, crc32.checksum());
                session->write(header, body);
                p = protocol::none;
                break;
            }

            default:
                p = protocol::none;
        }

        session->close(false);
    }).detach();


    std::thread threads[2];
    threads[0] = std::thread([server] { server->context()->run(); });
    threads[1] = std::thread([client] { client->context()->run(); });

    for(auto& th : threads)
        th.join();

    return 0;
}
catch (boost::system::system_error& e)
{
    std::cerr << e.code() << ' ' << '-' << ' ' << e.what() << std::endl;
}
catch (std::exception& e)
{
    std::cerr << e.what() << std::endl;
}
catch (...)
{
    std::cerr << "unknown exception" << std::endl;
    throw;
}
