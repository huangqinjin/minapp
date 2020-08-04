#ifndef MINAPP_FWD_HPP
#define MINAPP_FWD_HPP

#include <memory>
#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/io_context.hpp>


// Windows does not support abstract unix domain sockets yet.
// https://github.com/microsoft/WSL/issues/4240
// https://github.com/boostorg/asio/pull/311
// UNIX domain sockets.
#if !defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
# if !defined(BOOST_ASIO_DISABLE_LOCAL_SOCKETS)
#  if !defined(BOOST_ASIO_WINDOWS_RUNTIME)
#   define BOOST_ASIO_HAS_LOCAL_SOCKETS 1
#  endif // !defined(BOOST_ASIO_WINDOWS_RUNTIME)
# endif // !defined(BOOST_ASIO_DISABLE_LOCAL_SOCKETS)
#endif // !defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

#if defined(BOOST_ASIO_WINDOWS) && !defined(BOOST_ASIO_WINDOWS_RUNTIME) || defined(__CYGWIN__)
namespace boost::asio::detail {
struct sockaddr_un_type { unsigned short sun_family;
  char sun_path[108]; }; /* copy from afunix.h */
}
#endif


#define MINAPP_EXPORT BOOST_SYMBOL_EXPORT
#define MINAPP_IMPORT BOOST_SYMBOL_IMPORT

#if defined(minapp_EXPORTS) // defined by cmake DEFINE_SYMBOL property
#  define MINAPP_API MINAPP_EXPORT
#elif MINAPP_SHARED_LIBRARY
#  define MINAPP_API MINAPP_IMPORT
#else
#  define MINAPP_API
#endif

/**
 * reference type:
 *     - s  shared ptr
 *     - w  weak ptr
 *     - r  raw ptr
 *
 *     ______________________________________
 *    |                 s                    |
 *    v                                      |
 * service --s--> session_manager ---r--> session --r--> socket
 *    |                                      |
 *    |------s-----> context                 |------r--> persistent_buffer_manager
 *    |                                      |
 *    |------s-----> handler <-------s-------|------r--> attribute_set
 */

namespace minapp
{
    class persistent_buffer;
    class persistent_buffer_list;
    class persistent_buffer_manager;
    class buffer;
    class session;
    class session_manager;
    class handler;
    class service;
    class connector;
    class acceptor;
    class attribute_set;

    using endpoint = boost::asio::generic::stream_protocol::endpoint;
    using socket = boost::asio::generic::stream_protocol::socket;
    using context = boost::asio::io_context;

    using session_ptr = std::shared_ptr<session>;
    using session_manager_ptr = std::shared_ptr<session_manager>;
    using handler_ptr = std::shared_ptr<handler>;
    using service_ptr = std::shared_ptr<service>;
    using connector_ptr = std::shared_ptr<connector>;
    using acceptor_ptr = std::shared_ptr<acceptor>;
    using context_ptr = std::shared_ptr<context>;
}

#endif //MINAPP_FWD_HPP
