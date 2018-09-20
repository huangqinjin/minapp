//
// Created by huangqinjin on 18-6-28.
//

#ifndef MINAPP_FWD_HPP
#define MINAPP_FWD_HPP

#include <memory>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>

/**
 * reference type:
 *     - s  shared ptr
 *     - w  weak ptr
 *     - r  raw ptr
 *
 *     _____________________________________
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
    class session;
    class session_manager;
    class handler;
    class service;
    class connector;
    class acceptor;
    class attribute_set;

    using endpoint = boost::asio::ip::tcp::endpoint;
    using socket = boost::asio::ip::tcp::socket;
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
