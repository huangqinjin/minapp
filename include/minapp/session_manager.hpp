#ifndef MINAPP_SESSION_MANAGER_HPP
#define MINAPP_SESSION_MANAGER_HPP

#include "fwd.hpp"
#include "object.hpp"

namespace minapp
{
    class MINAPP_API session_manager :
        public std::enable_shared_from_this<session_manager>
    {
    public:
        static session_manager_ptr create();

        session_ptr create(service_ptr service);
        session_ptr get(unsigned long id);

        /*
            bool f(session* session)
            return value:
                true: continue;
                false: stop.
        */
        std::size_t foreach(object::fn<bool(&)(session*)> f);
    };
}

#endif

