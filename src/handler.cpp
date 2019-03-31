#include <minapp/handler.hpp>
#include <minapp/service.hpp>

using namespace minapp;

namespace
{
    enum runtime_errc
    {
        unknown_connect_exception,
        unknown_read_exception,
        unknown_write_exception,
        unknown_except_exception,
        unknown_error_exception,
        unknown_close_exception,
    };

    class runtime_category : public boost::system::error_category
    {
    public:
        static const runtime_category& instance()
        {
            static runtime_category instance_;
            return instance_;
        }

        const char* name() const BOOST_SYSTEM_NOEXCEPT override
        {
            return "runtime";
        }

        std::string message(int value) const override
        {
#define MINAPP_RUNTIME_CATEGORY_ERROR_MESSAGE(h) case unknown_ ## h ## _exception: return "Unexpected exception from "#h" handler";
            switch (value)
            {
                MINAPP_RUNTIME_CATEGORY_ERROR_MESSAGE(connect);
                MINAPP_RUNTIME_CATEGORY_ERROR_MESSAGE(read);
                MINAPP_RUNTIME_CATEGORY_ERROR_MESSAGE(write);
                MINAPP_RUNTIME_CATEGORY_ERROR_MESSAGE(except);
                MINAPP_RUNTIME_CATEGORY_ERROR_MESSAGE(error);
                MINAPP_RUNTIME_CATEGORY_ERROR_MESSAGE(close);
                default: break;
            }
            return "runtime error";
#undef MINAPP_RUNTIME_CATEGORY_ERROR_MESSAGE
        }
    };
}


handler_ptr handler::dummy()
{
    static auto dummy_handler = std::make_shared<noexcept_handler>();
    return dummy_handler;
}

handler_ptr noexcept_handler::wrapped() noexcept
{
    return {};
}

void noexcept_handler_impl::connect(session* session, const endpoint& ep) noexcept
{
    try
    {
        connect_impl(session, ep);
    }
    catch (std::exception& e)
    {
        except(session, e);
    }
    catch (...)
    {
        error(session, boost::system::error_code(unknown_connect_exception, runtime_category::instance()));
    }
}

void noexcept_handler_impl::read(session* session, buffer& buf) noexcept
{
    try
    {
        read_impl(session, buf);
    }
    catch (std::exception& e)
    {
        except(session, e);
    }
    catch (...)
    {
        error(session, boost::system::error_code(unknown_read_exception, runtime_category::instance()));
    }
}

void noexcept_handler_impl::write(session* session, persistent_buffer_list& list) noexcept
{
    try
    {
        write_impl(session, list);
    }
    catch (std::exception& e)
    {
        except(session, e);
    }
    catch (...)
    {
        error(session, boost::system::error_code(unknown_write_exception, runtime_category::instance()));
    }
}

void noexcept_handler_impl::except(session* session, std::exception& e) noexcept
{
    try
    {
        except_impl(session, e);
    }
    catch (...)
    {
        error(session, boost::system::error_code(unknown_except_exception, runtime_category::instance()));
    }
}

void noexcept_handler_impl::error(session* session, boost::system::error_code ec) noexcept
{
    try
    {
        error_impl(session, std::move(ec));
    }
    catch (...)
    {
        try
        {
            error_impl(session, boost::system::error_code(unknown_error_exception, runtime_category::instance()));
        }
        catch (...)
        {
            // \todo log something
        }
    }
}

void noexcept_handler_impl::close(session* session) noexcept
{
    try
    {
        close_impl(session);
    }
    catch (std::exception& e)
    {
        except(session, e);
    }
    catch (...)
    {
        error(session, boost::system::error_code(unknown_close_exception, runtime_category::instance()));
    }
}

std::shared_ptr<noexcept_handler> noexcept_handler::wrap(handler_ptr handler)
{
    if (handler)
    {
        if (nullptr == dynamic_cast<noexcept_handler*>(handler.get()))
        {
            class noexcept_handler_wrapper : public noexcept_handler_impl
            {
                handler_ptr handler_;

            public:
                explicit noexcept_handler_wrapper(handler_ptr handler) : handler_(std::move(handler)) {}

            private:
                void connect_impl(session* session, const endpoint& ep) override
                {
                    handler_->connect(session, ep);
                }

                void read_impl(session* session, buffer& buf) override
                {
                    handler_->read(session, buf);
                }

                void write_impl(session* session, persistent_buffer_list& list) override
                {
                    handler_->write(session, list);
                }

                void except_impl(session* session, std::exception& e) override
                {
                    handler_->except(session, e);
                }

                void error_impl(session* session, boost::system::error_code ec) override
                {
                    handler_->error(session, std::move(ec));
                }

                void close_impl(session* session) override
                {
                    handler_->close(session);
                }

            };

            handler = std::make_shared<noexcept_handler_wrapper>(std::move(handler));
        }
    }
    else
    {
        handler = dummy();
    }
    return std::static_pointer_cast<noexcept_handler>(std::move(handler));
}
