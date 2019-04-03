#ifndef MINAPP_SERVICE_HPP
#define MINAPP_SERVICE_HPP

#include "fwd.hpp"
#include "attribute_set.hpp"

#include <future>


namespace minapp
{
    class MINAPP_API service :
        public std::enable_shared_from_this<service>
    {
    protected:
        handler_ptr handler_;
        session_manager_ptr manager_;
        context_ptr context_;
        std::future<session_ptr> connect_impl(const endpoint& ep, handler_ptr handler, attribute_set attrs);

    public:
        virtual ~service() = 0;
        const handler_ptr& handler();
        const session_manager_ptr& manager();
        const context_ptr& context();
        std::future<session_ptr> connect(const endpoint& ep, attribute_set attrs = {});
        std::future<session_ptr> connect(const endpoint& ep, handler_ptr handler, attribute_set attrs = {});
        std::future<session_ptr> connect(const endpoint& ep, std::function<void(session*, boost::system::error_code)> callback);
    };
}
#endif