#define BOOST_ENDIAN_NO_CTORS
#include <boost/endian/buffers.hpp>

#include "utils.hpp"

using namespace boost::asio::ip;
using namespace boost::endian;

// RFC1928 SOCKS Protocol Version 5
// RFC1929 Username/Password Authentication for SOCKS V5
class socks5 : public logging::handler
{
    using session_handle = std::weak_ptr<session>;

    void connect(session* session, const endpoint& ep) override
    {
        session_handle h;
        session->attrs.get("PEER", h);
        if (auto peer = h.lock()) // sock5 -> server
        {
            NSLOG(CONN) << "PIPE " << peer->id();

            peer->attrs.set("PEER", session->weak_from_this());

            auto& remote = reinterpret_cast<tcp::endpoint const&>(ep);

            if (remote.address().is_v4())
            {
                struct
                {
                    big_uint8_buf_t VER;
                    big_uint8_buf_t REP;
                    big_uint8_buf_t RSV;
                    big_uint8_buf_t ATYP;
                    address_v4::bytes_type ADDR;
                    big_uint16_buf_t PORT;
                } reply;

                reply.VER = 5;
                reply.REP = 0;
                reply.RSV = 0;
                reply.ATYP = 1;
                reply.ADDR = remote.address().to_v4().to_bytes();
                reply.PORT = remote.port();
                peer->write(reply);
            }
            else
            {
                struct
                {
                    big_uint8_buf_t VER;
                    big_uint8_buf_t REP;
                    big_uint8_buf_t RSV;
                    big_uint8_buf_t ATYP;
                    address_v6::bytes_type ADDR;
                    big_uint16_buf_t PORT;
                } reply;

                reply.VER = 5;
                reply.REP = 0;
                reply.RSV = 0;
                reply.ATYP = 4;
                reply.ADDR = remote.address().to_v6().to_bytes();
                reply.PORT = remote.port();
                peer->write(reply);
            }
        }
        else // client -> socks5
        {
            session->protocol(protocol::fixed, 1);
        }
    }

    void connect_to(session* session, const tcp::endpoint& ep, object::fn<endpoint()> gen)
    {
        attribute_set attrs = { {"PEER", session->weak_from_this()} };
        session->protocol(protocol::any, 65536);
        if (gen) return (void)session->service()->connect(std::move(gen), std::move(attrs));
        session->service()->connect(ep, std::move(attrs));
        NSLOG(READ) << "TARGET " << ep;
    }

    void read(session* session, buffer& buf) override
    {
        if (session_handle h; session->attrs.get("PEER", h))
        {
            if (auto peer = h.lock())
                peer->write(buf);
            else
                session->close();
            return;
        }

        union
        {
            const char* whole;

            struct
            {
                big_uint8_buf_t VER;
                big_uint8_buf_t NMETHODS;
                big_uint8_buf_t METHODS[];
            } const * methods;

            struct
            {
                big_uint8_buf_t VER;
                big_uint8_buf_t ULEN;
                char UNAME[];
            } const * uname;

            struct
            {
                big_uint8_buf_t VER;
                big_uint8_buf_t CMD;
                big_uint8_buf_t RSV;
                big_uint8_buf_t ATYP;
                union
                {
                    address_v4::bytes_type V4ADDR;
                    address_v6::bytes_type V6ADDR;
                    struct
                    {
                        big_uint8_buf_t DLEN;
                        char DOMAIN[];
                    };
                } DST;
            } const * request;
        };
        whole = static_cast<const char*>(buf.whole().data());

        union
        {
            const char* data;

            struct
            {
                big_uint8_buf_t PLEN;
                char PASSWD[];
            } const * passwd;

            struct
            {
                big_uint16_buf_t PORT;
            } const * DST;
        };
        data = static_cast<const char*>(buf.data());

#include <boost/asio/yield.hpp>
        reenter(session) {

            NSLOG(READ) << "Authentication method negotiation";

            if (methods->VER.value() != 5)
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument("Unsupported METHODS VER"));
            }

            yield session->protocol(protocol::prefix_8, 256, protocol_options::do_not_consume_buffer);

            if (std::any_of(methods->METHODS, methods->METHODS + methods->NMETHODS.value(),
                           [](auto m) { return m.value() == 2; }))
            {
                struct
                {
                    big_uint8_buf_t VER;
                    union
                    {
                        big_uint8_buf_t METHOD;
                        big_uint8_buf_t STATUS;
                    };
                } reply;

                reply.VER = 5;
                reply.METHOD = 2;
                session->write(reply);

                NSLOG(READ) << "METHOD: USERNAME/PASSWORD (2), method-specific sub-negotiation";

                yield session->protocol(protocol::fixed, 1);

                if (methods->VER.value() != 1)
                {
                    BOOST_THROW_EXCEPTION(std::invalid_argument("Unsupported USERNAME/PASSWORD VER"));
                }

                yield session->protocol(protocol::prefix_8, 256, protocol_options::do_not_consume_buffer);
                yield session->protocol(protocol::prefix_8, 256, protocol_options::do_not_consume_buffer);

                const std::string usr(uname->UNAME, uname->ULEN.value());
                const std::string pwd(passwd->PASSWD, passwd->PLEN.value());

                NSLOG(READ) << "USR:PWD " << usr << ':' << pwd;

                reply.VER = 1;
                reply.STATUS = 0;
                session->write(reply);
            }
            else if (std::any_of(methods->METHODS, methods->METHODS + methods->NMETHODS.value(),
                                   [](auto m) { return m.value() == 0; }))
            {
                struct
                {
                    big_uint8_buf_t VER;
                    big_uint8_buf_t METHOD;
                } reply;

                reply.VER = 5;
                reply.METHOD = 0;
                session->write(reply);

                NSLOG(READ) << "METHOD: NO AUTHENTICATION REQUIRED (0)";
            }
            else
            {
                struct
                {
                    big_uint8_buf_t VER;
                    big_uint8_buf_t METHOD;
                } reply;

                reply.VER = 5;
                reply.METHOD = 0xFF;
                session->write(reply);

                NSLOG(READ) << "METHOD: NO ACCEPTABLE METHODS (0xFF)";
                yield break;
            }

            NSLOG(READ) << "SOCKS request";

            yield session->protocol(protocol::fixed, 4);

            if (request->VER.value() != 5)
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument("Unsupported SOCKS request VER"));
            }
            if (request->CMD.value() != 1)
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument("Unsupported SOCKS request CMD"));
            }
            if (request->RSV.value() != 0)
            {
                BOOST_THROW_EXCEPTION(std::invalid_argument("Unsupported SOCKS request RSV"));
            }

            if (request->ATYP.value() == 1)
            {
                NSLOG(READ) << "ATYP: IP V4 address (1)";

                yield session->protocol(protocol::fixed, 4, protocol_options::do_not_consume_buffer);
                yield session->protocol(protocol::fixed, 2, protocol_options::do_not_consume_buffer);

                connect_to(session, { address_v4(request->DST.V4ADDR), DST->PORT.value() }, {});
                yield break;
            }
            if (request->ATYP.value() == 4)
            {
                NSLOG(READ) << "ATYP: IP V6 address (4)";

                yield session->protocol(protocol::fixed, 16, protocol_options::do_not_consume_buffer);
                yield session->protocol(protocol::fixed, 2, protocol_options::do_not_consume_buffer);

                connect_to(session, { address_v6(request->DST.V6ADDR), DST->PORT.value()}, {});
                yield break;
            }
            if (request->ATYP.value() == 3)
            {
                NSLOG(READ) << "ATYP: DOMAINNAME (3)";

                yield session->protocol(protocol::prefix_8, 257, protocol_options::do_not_consume_buffer);
                yield session->protocol(protocol::fixed, 2, protocol_options::do_not_consume_buffer);
            }
            else
            {
                NSLOG(READ) << "ATYP unsupported " << (int)request->ATYP.value();
                BOOST_THROW_EXCEPTION(std::invalid_argument("Unsupported SOCKS request ATYP"));
            }

            const std::string_view domain(request->DST.DOMAIN, request->DST.DLEN.value());
            const unsigned short port = DST->PORT.value();

            NSLOG(READ) << "DOMAIN " << domain << ':' << port;

            session->servlet<tcp::resolver>()->async_resolve(domain, std::to_string(port),
                [session = session->shared_from_this(), this](
                   const boost::system::error_code& ec,
                   tcp::resolver::results_type results)
            {
                session->servlets::del<tcp::resolver>();

                std::transform(
                    results.cbegin(), results.cend(),
                    std::ostream_iterator<const tcp::endpoint&>(NSLOG(READ) << "TARGETS ", ", "),
                    [](const auto& e) { return e.endpoint(); }
                );

                connect_to(session.get(), tcp::endpoint(),
                           [this, session, results, iter = results.cbegin()]() mutable -> endpoint
                {
                    if (iter == results.cend()) return {};
                    auto ep = (iter++)->endpoint();
                    NSLOG(READ) << "TARGET " << ep;
                    return ep;
                });
            });
        }
#include <boost/asio/unyield.hpp>
    }

public:
    socks5() noexcept : handler("socks5")
    {
        log_read(false);
        log_write(false);
    }
};


int main(int argc, char* argv[]) try
{
    endpoint ep;
    if (argc == 1) ep = tcp::endpoint(tcp::v6(), 1080);
    else ep = make_endpoint(true, argv[2] ? argv[2] : "ipv6", argv[1]);

    auto server = acceptor::create(logging::wrap<socks5>());

    workers workers({server}, 1);

    server->bind(ep);

    std::string line;
    while (getline(std::cin, line))
        logging::stream() << line;
}
catch (boost::system::system_error& e)
{
    std::cerr << e.code() << ' ' << '-' << ' ' << e.what() << std::endl;
    return 1;
}
catch (std::exception& e)
{
    std::cerr << e.what() << std::endl;
    return 1;
}
catch (...)
{
    std::cerr << "unknown exception" << std::endl;
    throw;
}
