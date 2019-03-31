#ifndef MINAPP_ACCEPTOR_HPP
#define MINAPP_ACCEPTOR_HPP

#include "service.hpp"

namespace minapp
{
    class acceptor : public service
    {
        boost::asio::ip::tcp::acceptor acceptor_;
        boost::asio::executor_work_guard<context::executor_type> guard_;

        acceptor(handler_ptr handler, context_ptr ctx);
        ~acceptor() override;

    public:
        static acceptor_ptr create(handler_ptr handler, context_ptr ctx = {});
        acceptor_ptr shared_from_this();
        void bind(const endpoint& ep);
        void unbind();

    private:
        void accept();
    };
}
#endif