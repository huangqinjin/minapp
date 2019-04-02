#include <minapp/handler.hpp>
#include <minapp/session.hpp>
#include <minapp/session_manager.hpp>
#include <minapp/service.hpp>
#include <minapp/connector.hpp>
#include <minapp/acceptor.hpp>

using namespace minapp;

service::~service() = default;

const handler_ptr& service::handler()
{
    return handler_;
}

const session_manager_ptr& service::manager()
{
    return manager_;
}

const context_ptr& service::context()
{
    return context_;
}

std::future<session_ptr> service::connect_impl(const endpoint& ep, handler_ptr handler, attribute_set attrs)
{
    auto session = manager_->create(shared_from_this());
    session->attrs.swap(attrs);
    session->handler_ = std::move(handler);
    return session->connect(ep);
}

std::future<session_ptr> service::connect(const endpoint& ep, attribute_set attrs)
{
    return connect_impl(ep, handler_, std::move(attrs));
}

std::future<session_ptr> service::connect(const endpoint& ep, handler_ptr handler, attribute_set attrs)
{
    return connect_impl(ep, noexcept_handler::wrap(std::move(handler)), std::move(attrs));
}

std::future<session_ptr> service::connect(
        const endpoint& ep,
        std::function<void(session*, boost::system::error_code)> callback)
{
    class connect_handler : public noexcept_handler_impl
    {
        using connector = std::function<void(session*, boost::system::error_code)>;
        connector conn;

    public:
        explicit connect_handler(connector conn) : conn(std::move(conn)) {}

    private:
        void connect_impl(session* session, const minapp::endpoint& ep) override
        {
            session->handler({});
            if(connector conn = std::move(this->conn)) conn(session, {});
            else session->handler()->connect(session, ep);
        }

        void error_impl(session* session, boost::system::error_code ec) override
        {
            session->handler({});
            if(connector conn = std::move(this->conn)) conn(session, std::move(ec));
            else session->handler()->error(session, std::move(ec));
        }
    };

    return connect_impl(ep, std::make_shared<connect_handler>(std::move(callback)), {});
}

connector::connector(const endpoint& remote, handler_ptr handler, context_ptr ctx)
    : guard_(boost::asio::make_work_guard(ctx ? *ctx : *(ctx = std::make_shared<::context>())))
{
    remote_ = remote;
    context_ = std::move(ctx);
    handler_ = noexcept_handler::wrap(std::move(handler));
    manager_ = session_manager::create();
}

connector::~connector() = default;

connector_ptr connector::create(const endpoint& remote, handler_ptr handler, context_ptr ctx)
{
    struct make_public_ctor : connector
    {
        make_public_ctor(const endpoint& remote, handler_ptr handler, context_ptr ctx)
            : connector(remote, std::move(handler), std::move(ctx)) {}
    };
    return std::make_shared<make_public_ctor>(remote, std::move(handler), std::move(ctx));
}

connector_ptr connector::create(handler_ptr handler, context_ptr ctx)
{
    return create(endpoint{}, std::move(handler), std::move(ctx));
}

connector_ptr connector::shared_from_this()
{
    return std::static_pointer_cast<connector>(service::shared_from_this());
}

const endpoint& connector::remote() const
{
    return remote_;
}

std::future<session_ptr> connector::connect()
{
    return service::connect(remote_);
}

std::future<session_ptr> connector::connect(handler_ptr handler)
{
    return service::connect(remote_, std::move(handler));
}

std::future<session_ptr> connector::connect(std::function<void(session*, boost::system::error_code)> callback)
{
    return service::connect(remote_, std::move(callback));
}


acceptor::acceptor(handler_ptr handler, context_ptr ctx)
    : acceptor_(ctx ? *ctx : *(ctx = std::make_shared<::context>())),
      guard_(boost::asio::make_work_guard(*ctx))
{
    context_ = std::move(ctx);
    handler_ = noexcept_handler::wrap(std::move(handler));
    manager_ = session_manager::create();
}

acceptor::~acceptor() = default;

acceptor_ptr acceptor::create(handler_ptr handler, context_ptr ctx)
{
    struct make_public_ctor : acceptor
    {
        make_public_ctor(handler_ptr handler, context_ptr ctx)
                : acceptor(std::move(handler), std::move(ctx)) {}
    };
    return std::make_shared<make_public_ctor>(std::move(handler), std::move(ctx));
}

std::shared_ptr<acceptor> acceptor::shared_from_this()
{
    return std::static_pointer_cast<acceptor>(service::shared_from_this());
}

void acceptor::bind(const endpoint& ep)
{
    acceptor_.open(ep.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();
    accept();
}

void acceptor::unbind()
{
    acceptor_.cancel();
}

void acceptor::accept()
{
    acceptor_.async_accept([self = shared_from_this()]
    (const boost::system::error_code& ec, socket socket)
    {
        if (ec)
        {
            throw boost::system::system_error(ec);
        }
        else
        {
            self->accept();
            auto session = self->manager_->create(self);
            session->socket_ = std::move(socket);
            session->handler_ = self->handler_;
            session->connect();
        }
    });
}
