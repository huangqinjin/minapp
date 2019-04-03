#ifndef MINAPP_SESSION_HPP
#define MINAPP_SESSION_HPP

#include "fwd.hpp"
#include "persistent_buffer_manager.hpp"
#include "buffer.hpp"
#include "protocol.hpp"
#include "attribute_set.hpp"

#include <future>
#include <atomic>

#include <boost/asio/coroutine.hpp>

namespace minapp
{
    enum class status
    {
        connecting,
        connected,
        reading,
        closing,
        closed,
    };

    class MINAPP_API session
        : public std::enable_shared_from_this<session>,
          public boost::asio::coroutine
    {
        friend class service;
        friend class acceptor;
        friend class connector;
        friend class session_manager;

    private:
        const unsigned long id_;
        const service_ptr service_;
        minapp::socket socket_;
        handler_ptr handler_;
        persistent_buffer_manager write_queue_;
        triple_buffer buf_;
        enum protocol protocol_;
        enum protocol_options protocol_options_;
        std::atomic<enum status> status_;
        std::size_t read_buffer_size_;
        std::string delimiter_;

    public:
        attribute_set attrs;

        explicit session(service_ptr service);
        ~session();

        unsigned long id() const;
        std::size_t read_buffer_size() const;
        void read_buffer_size(std::size_t sz);
        const std::string& delimiter() const;
        void delimiter(char delim);
        void delimiter(std::string delim);
        enum protocol protocol() const;
        enum protocol_options protocol_options() const;
        void protocol(enum protocol protocol, enum protocol_options options = {});
        enum status status() const;
        minapp::socket& socket();
        const service_ptr& service() const;
        handler_ptr handler() const;
        handler_ptr handler(handler_ptr other);
        handler_ptr use_service_handler();


        template<typename ...Buffers>
        void write(Buffers&&... buffers)
        {
            write_queue_.manage(std::forward<Buffers>(buffers)...);
            write();
        }

        void write(persistent_buffer_list& list)
        {
            write_queue_.manage(list);
            write();
        }

        void write(persistent_buffer_list&& list)
        {
            write(list);
        }

        void close();

    private:
        bool check(const boost::system::error_code& ec);
        std::future<session_ptr> connect(const endpoint& ep);
        void connect();
        void write();
        void read();
        void read_some(std::size_t bufsize);
        void read_fixed(std::size_t bufsize);
        void read_delim(char delim);
        void read_delim(const std::string& delim);
        void read_prefix(std::size_t len);
    };
}




#endif