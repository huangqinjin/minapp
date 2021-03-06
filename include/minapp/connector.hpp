#ifndef MINAPP_CONNECTOR_HPP
#define MINAPP_CONNECTOR_HPP

#include "service.hpp"

namespace minapp
{
    class MINAPP_API connector : public service
    {
    public:
        static connector_ptr create(handler_ptr handler, context_ptr ctx = {});
        static connector_ptr create(const endpoint& remote, handler_ptr handler, context_ptr ctx = {});
        connector_ptr shared_from_this();
        const endpoint& remote() const;

        using service::connect;
        std::future<session_ptr> connect();
        std::future<session_ptr> connect(handler_ptr handler);
        std::future<session_ptr> connect(std::function<void(session*, boost::system::error_code)> callback);
    };
}
#endif