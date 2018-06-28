#ifndef MINAPP_HANDLER_HPP
#define MINAPP_HANDLER_HPP

#include "fwd.hpp"

namespace minapp
{
    class handler :
        public std::enable_shared_from_this<handler>
    {
    public:
        static handler_ptr dummy();

        virtual ~handler() {}

        virtual void connect(session* session, const endpoint& endpoint) {}

        virtual void read(session* session, boost::asio::streambuf& buffer) {}

        virtual void write(session* session, persistent_buffer_list& list) {}

        virtual void except(session* session, std::exception& e) {}

        virtual void error(session* session, boost::system::error_code ec) {}

        virtual void close(session* session) {}
    };

    class wrapped_handler : public handler
    {
    public:
        virtual handler_ptr wrapped() noexcept = 0;
    };

    class noexcept_handler : public wrapped_handler
    {
    public:
        static std::shared_ptr<noexcept_handler> wrap(handler_ptr handler);

        handler_ptr wrapped() noexcept override;

        void connect(session* session, const endpoint& endpoint) noexcept override {}

        void read(session* session, boost::asio::streambuf& buffer) noexcept override {}

        void write(session* session, persistent_buffer_list& list) noexcept override {}

        void except(session* session, std::exception& e) noexcept override {}

        void error(session* session, boost::system::error_code ec) noexcept override {}

        void close(session* session) noexcept override {}
    };

    class noexcept_handler_impl : public noexcept_handler
    {
    public:
        void connect(session* session, const boost::asio::ip::tcp::endpoint& endpoint) noexcept final;

        void read(session* session, boost::asio::streambuf& buffer) noexcept final;

        void write(session* session, persistent_buffer_list& list) noexcept final;

        void except(session* session, std::exception& e) noexcept final;

        void error(session* session, boost::system::error_code ec) noexcept final;

        void close(session* session) noexcept final;

    protected:
        virtual void connect_impl(session* session, const endpoint& endpoint) {}

        virtual void read_impl(session* session, boost::asio::streambuf& buffer) {}

        virtual void write_impl(session* session, persistent_buffer_list& list) {}

        virtual void except_impl(session* session, std::exception& e) {}

        virtual void error_impl(session* session, boost::system::error_code ec) {}

        virtual void close_impl(session* session) {}
    };
}
#endif