#include "utils.hpp"

class forward : public logging::handler
{
    using session_handle = std::weak_ptr<session>;

    void connect(session* session, const endpoint& ep) override
    {
        session_handle h;
        if (!session->attrs.get("PEER", h))
        {
            session->service()->connect(remote, { {"PEER", session->weak_from_this()} });
        }
        else if (auto peer = h.lock())
        {
            peer->attrs.set("PEER", session->weak_from_this());
        }
        else
        {
            session->close(true);
        }
    }

    void read(session* session, buffer& buf) override
    {
        session_handle h;
        if (session->attrs.get("PEER", h))
        {
            session->protocol(protocol::any);
            if (auto peer = h.lock())
                peer->write(buf.whole());
            else
                session->close();
        }
        else
        {
            session->protocol(protocol::any, protocol_options::do_not_consume_buffer);
        }
    }

    void close(session* session) override
    {
        session_handle h;
        if (session->attrs.get("PEER", h))
        {
            if (auto peer = h.lock())
                peer->close(true);
        }
    }

    endpoint remote;

public:
    explicit forward(const endpoint& remote)
        : handler("forward"), remote(remote)
    {
        log_read(false);
        log_write(false);
    }
};

int main(int argc, char* argv[]) try
{
    if (argc != 3 && argc != 4)
    {
        std::cout << "Usage: " << argv[0] << " <from> <to> [protocol]" << std::endl;
        return -1;
    }

    auto pair = make_endpoint_pair(argv[3], argv[1], argv[2]);

    auto server = minapp::acceptor::create(logging::wrap<forward>(pair.second));

    workers workers({server}, 1);

    server->bind(pair.first);

    std::string line;
    while (getline(std::cin, line))
        logging::stream() << line;

    return 0;
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
