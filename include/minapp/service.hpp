#ifndef MINAPP_SERVICE_HPP
#define MINAPP_SERVICE_HPP

#include "fwd.hpp"
#include "attribute_set.hpp"

#include <future>


namespace minapp
{
    class service :
        public std::enable_shared_from_this<service>
    {
    protected:
        handler_ptr handler_;
        session_manager_ptr manager_;
        context_ptr context_;
        std::future<session_ptr> connect_impl(const endpoint& endpoint, handler_ptr handler, attribute_set attrs);

    public:
        virtual ~service() = 0;
        const handler_ptr& handler();
        const session_manager_ptr& manager();
        const context_ptr& context();
        std::future<session_ptr> connect(const endpoint& endpoint, attribute_set attrs = {});
        std::future<session_ptr> connect(const endpoint& endpoint, handler_ptr handler, attribute_set attrs = {});
        std::future<session_ptr> connect(const endpoint& endpoint, std::function<void(session*, const minapp::endpoint&)> callback);
    };
}
#endif