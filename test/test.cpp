#include <minapp/connector.hpp>
#include <minapp/acceptor.hpp>
#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/memory_printer.hpp>

#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>

#include <boost/endian/conversion.hpp>

std::mutex ostream_lock;
struct ostream_guard
{
    ostream_guard() { ostream_lock.lock(); }
    ~ostream_guard() { stream() << std::endl; ostream_lock.unlock(); }
    std::ostream& stream() { return std::cout; }
};

#define LOG(TAG) ostream_guard{}.stream() << __FILE__ << ':' << __LINE__ << " [" << #TAG << "] - "


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

    handler_ptr wrapped() noexcept override
    {
        return h;
    }

    void connect_impl(session* session, const endpoint& ep) override
    {
        LOG(CONN) << '[' << h->name() << ':' << session->id() << "] connect to " << ep;
        h->connect(session, ep);
    }

    void read_impl(session* session, buffer& buf) override
    {
        std::size_t size = buf.size();
        const void* p = buf.data();
        streambuf_memory_printer{LOG(READ) << '[' << h->name() << ':' << session->id() << "] bufsize = " << size << '\n'}(p, size);
        h->read(session, buf);
    }

    void write_impl(session* session, persistent_buffer_list& list) override
    {
        for(auto& buf : list)
        {
            std::size_t size = buf.size();
            const void* p = buf.data();
            streambuf_memory_printer{(LOG(WRITE) << '[' << h->name() << ':' << session->id() << "] bufsize = " << size << '\n')}(p, size);
        }
        h->write(session, list);
    }

    void except_impl(session* session, std::exception& e) override
    {
        LOG(EXCEPT) << '[' << h->name() << ':' << session->id() << "] " << e.what();
        h->except(session, e);
    }

    void error_impl(session* session, boost::system::error_code ec) override
    {
        LOG(ERROR) << '[' << h->name() << ':' << session->id() << "] " << ec.message();
        h->error(session, ec);
    }

    void close_impl(session* session) override
    {
        LOG(CLOSE) << '[' << h->name() << ':' << session->id() << "] ";
        h->close(session);
    }
};


class ServerHandler : public logging_handler::named
{
    std::string name() const override
    {
        return "server";
    }

    void connect(session* session, const endpoint& ep) override
    {
        session->protocol(protocol::prefix_32, protocol_options::use_little_endian | protocol_options::do_not_consume_buffer);
    }

    void read(session* session, buffer& buf) override
    {
        auto msg = persist(buf.whole());
        session->write(msg);
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
};


int main()
{
    auto server = minapp::acceptor::create(logging_handler::wrap<ServerHandler>());
    auto client = minapp::connector::create(logging_handler::wrap<ClientHandler>());

    server->bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 2333));

    std::thread([client] {
        auto future = client->connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 2333));
        auto session = future.get();

        struct header
        {
            std::uint32_t len;
            std::int32_t type;

            static persistent_buffer make(std::size_t len, int type)
            {
                header h;
                h.len = boost::endian::native_to_little(len + sizeof(h) - sizeof(h.len));
                h.type = boost::endian::native_to_little(type);
                return persist(h);
            }
        };

        auto msg = persist("sent");

        for(int i = 0; i <= 10; ++i)
        {
            auto random = persist(std::string(i, 'x'));
            auto list = make_list(msg, random);
            auto head = header::make(boost::asio::buffer_size(list), i);
            list.push_front(head);

            assert(head.storage().type() == typeid(header));
            assert(msg.storage().type() == typeid(char[5]));
            assert(random.storage().type() == typeid(std::string));

            session->write(list);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        session->close();
    }).detach();


    std::thread threads[2];
    threads[0] = std::thread([server] { server->context()->run(); });
    threads[1] = std::thread([client] { client->context()->run(); });

    for(auto& th : threads)
        th.join();

    return 0;
}