#ifndef MINAPP_CONNECTOR_HPP
#define MINAPP_CONNECTOR_HPP

#include "service.hpp"

namespace minapp
{
    class connector : public service
    {
        context::work work_;
        endpoint remote_;

        connector(const endpoint& remote, handler_ptr handler, context_ptr context);
        ~connector() override;

    public:
        static connector_ptr create(handler_ptr handler, context_ptr context = {});
        static connector_ptr create(const endpoint& remote, handler_ptr handler, context_ptr context = {});
        connector_ptr shared_from_this();
        const endpoint& remote() const;

        using service::connect;
        std::future<session_ptr> connect();
        std::future<session_ptr> connect(handler_ptr handler);
        std::future<session_ptr> connect(std::function<void(session*, const endpoint&)> callback);
    };
}
#endif