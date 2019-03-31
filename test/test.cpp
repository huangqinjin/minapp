#include <iostream>
#include <minapp/connector.hpp>
#include <minapp/acceptor.hpp>
#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/memory_printer.hpp>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>

std::mutex ostream_lock;
struct ostream_guard
{
    ostream_guard() { ostream_lock.lock(); }
    ~ostream_guard() { stream() << std::endl; ostream_lock.unlock(); }
    std::ostream& stream() { return std::cout; }
};

#define LOG(TAG) ostream_guard{}.stream() << __FILE__ << ':' << __LINE__ << " - [" << #TAG << "] "


using namespace minapp;

class ServerHandler : public noexcept_handler_impl
{
    void connect_impl(session* session, const endpoint& ep) override
    {
        LOG(server CONN) << "session[" << session->id() << "] connect from " << ep;

        session->protocol(protocol::prefix_32, protocol_options::use_little_endian);
    }

    void read_impl(session* session, buffer& buf) override
    {
        auto d = buf.data();
        unsigned size = d.size();
        const void* p = d.data();
        streambuf_memory_printer{LOG(server READ) << "session[" << session->id() << "] bufsize = " << size << '\n'}(p, size);

        auto msg = minapp::persist(std::vector<char>((const char*)p, ((const char*)p) + size));
        session->write(msg);
    }

    void write_impl(session* session, persistent_buffer_list& list) override
    {
        for(auto& buf : list)
        {
            unsigned size = buf.size();
            const void* p = buf.data();
            streambuf_memory_printer{(LOG(server WRITE) << "session[" << session->id() << "] bufsize = " << size << '\n')}(p, size);
        }
    }

    void except_impl(session* session, std::exception& e) override
    {
        LOG(server EXCEPT) << "session[" << session->id() << "] " << e.what();
    }

    void error_impl(session* session, boost::system::error_code ec) override
    {
        LOG(server ERROR) << "session[" << session->id() << "] " << ec.message();
    }

    void close_impl(session* session) override
    {
        LOG(server CLOSE) << "session[" << session->id() << "]";
    }
};

class ClientHandler : public minapp::noexcept_handler_impl
{
    void connect_impl(session* session, const endpoint& ep) override
    {
        LOG(client CONN) << "session[" << session->id() << "] connect to " << ep;

        session->protocol(protocol::prefix_32, protocol_options::use_little_endian);
    }

    void read_impl(session* session, buffer& buf) override
    {
        auto d = buf.data();
        unsigned size = d.size();
        const void* p = d.data();
        streambuf_memory_printer{LOG(client READ) << "session[" << session->id() << "] bufsize = " << size << '\n'}(p, size);
    }

    void write_impl(session* session, persistent_buffer_list& list) override
    {
        for(auto& buf : list)
        {
            unsigned size = buf.size();
            const void* p = buf.data();
            streambuf_memory_printer{(LOG(client WRITE) << "session[" << session->id() << "] bufsize = " << size << '\n')}(p, size);
        }
    }

    void except_impl(session* session, std::exception& e) override
    {
        LOG(client EXCEPT) << "session[" << session->id() << "] " << e.what();
    }

    void error_impl(session* session, boost::system::error_code ec) override
    {
        LOG(client ERROR) << "session[" << session->id() << "] " << ec.message();
    }

    void close_impl(session* session) override
    {
        LOG(client CLOSE) << "session[" << session->id() << "]";
    }
};


int main()
{
    auto server = minapp::acceptor::create(std::make_shared<ServerHandler>());
    auto client = minapp::connector::create(std::make_shared<ClientHandler>());

    server->bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 2333));

    std::thread([client] {
        auto future = client->connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 2333));
        auto session = future.get();
        for(int i = 0; i < 10; ++i)
        {
            std::string buf = "    sent " + std::to_string(i);
            uint32_t size = buf.size() - 4;
            std::memcpy(&buf[0], &size, 4);
            auto msg = minapp::persist(buf);
            session->write(msg);

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