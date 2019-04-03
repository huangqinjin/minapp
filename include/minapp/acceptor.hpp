#ifndef MINAPP_ACCEPTOR_HPP
#define MINAPP_ACCEPTOR_HPP

#include "service.hpp"

namespace minapp
{
    class MINAPP_API acceptor : public service
    {
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