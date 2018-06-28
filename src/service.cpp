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

std::future<session_ptr> service::connect_impl(const endpoint& endpoint, handler_ptr handler, attribute_set attrs)
{
    auto session = manager_->create(shared_from_this());
    session->attrs = std::move(attrs);
    session->handler_ = std::move(handler);
    return session->connect(endpoint);
}

std::future<session_ptr> service::connect(const endpoint& endpoint, attribute_set attrs)
{
    return connect_impl(endpoint, handler_, std::move(attrs));
}

std::future<session_ptr> service::connect(const endpoint& endpoint, handler_ptr handler, attribute_set attrs)
{
    return connect_impl(endpoint, noexcept_handler::wrap(std::move(handler)), std::move(attrs));
}

std::future<session_ptr> service::connect(
        const endpoint & endpoint,
        std::function<void(session*, const minapp::endpoint&)> callback)
{
    class connect_handler : public noexcept_handler_impl
    {
        using connector = std::function<void(session* session, const minapp::endpoint&)>;
        connector conn;

    public:
        explicit connect_handler(connector conn) : conn(std::move(conn)) {}

    private:
        void connect_impl(session* session, const minapp::endpoint& endpoint) override
        {
            session->handler({});
            if(connector conn = std::move(this->conn)) conn(session, endpoint);
            else session->handler()->connect(session, endpoint);
        }
    };

    return connect_impl(endpoint, std::make_shared<connect_handler>(std::move(callback)), {});
}

connector::connector(const endpoint& remote, handler_ptr handler, context_ptr context)
    : work_(context ? *context : *(context = std::make_shared<minapp::context>()))
{
    remote_ = remote;
    context_ = std::move(context);
    handler_ = noexcept_handler::wrap(std::move(handler));
    manager_ = session_manager::create();
}

connector::~connector() = default;

connector_ptr connector::create(const endpoint& remote, handler_ptr handler, context_ptr context)
{
    struct make_public_ctor : connector
    {
        make_public_ctor(const endpoint& remote, handler_ptr handler, context_ptr context)
            : connector(remote, std::move(handler), std::move(context)) {}
    };
    return std::make_shared<make_public_ctor>(remote, std::move(handler), std::move(context));
}

connector_ptr connector::create(handler_ptr handler, context_ptr context)
{
    return create(endpoint{}, std::move(handler), std::move(context));
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

std::future<session_ptr> connector::connect(std::function<void(session*, const endpoint&)> callback)
{
    return service::connect(remote_, std::move(callback));
}


acceptor::acceptor(handler_ptr handler, context_ptr context)
    : acceptor_(context ? *context : *(context = std::make_shared<minapp::context>())),
      socket_(*context), work_(*context)
{
    context_ = std::move(context);
    handler_ = noexcept_handler::wrap(std::move(handler));
    manager_ = session_manager::create();
}

acceptor::~acceptor() = default;

acceptor_ptr acceptor::create(handler_ptr handler, context_ptr context)
{
    struct make_public_ctor : acceptor
    {
        make_public_ctor(handler_ptr handler, context_ptr context)
                : acceptor(std::move(handler), std::move(context)) {}
    };
    return std::make_shared<make_public_ctor>(std::move(handler), std::move(context));
}

std::shared_ptr<acceptor> acceptor::shared_from_this()
{
    return std::static_pointer_cast<acceptor>(service::shared_from_this());
}

void acceptor::bind(const endpoint& endpoint)
{
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
    accept();
}

void acceptor::unbind()
{
    acceptor_.cancel();
}

void acceptor::accept()
{
    auto self = shared_from_this();
    acceptor_.async_accept(socket_, [self]
    (const boost::system::error_code& ec) mutable
    {
        if (ec)
        {
            throw boost::system::system_error(ec);
        }
        else
        {
            auto socket = std::move(self->socket_);
            self->accept();
            auto session = self->manager_->create(self);
            session->socket_ = std::move(socket);
            session->handler_ = self->handler_;
            session->connect();
        }
    });
}
