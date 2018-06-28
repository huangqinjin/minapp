#ifndef MINAPP_STREAMBUF_HPP
#define MINAPP_STREAMBUF_HPP

#include <boost/asio/streambuf.hpp>

namespace minapp
{
    template <typename Allocator = std::allocator<char>>
    class basic_streambuf : public boost::asio::basic_streambuf<Allocator>
    {
    public:
        typedef boost::asio::basic_streambuf<Allocator> inherited;

        using inherited::gbump; // advances the next pointer in the input sequence
        using inherited::eback; // beginning
        using inherited::gptr;  // current
        using inherited::egptr; // end
        using inherited::setg;  // repositions the beginning, next, and end pointers of the input sequence

        using inherited::pbump; // advances the next pointer of the output sequence
        using inherited::pbase; // beginning
        using inherited::pptr;  // current
        using inherited::epptr; // end
        using inherited::setp;  // repositions the beginning, next, and end pointers of the output sequence
        using typename inherited::mutable_buffers_type;
        using typename inherited::char_type;
        using typename inherited::traits_type;

        mutable_buffers_type reserve(std::size_t n)
        {
            auto buffer = this->prepare(n);
            pbump(static_cast<int>(n));
            setg(eback(), gptr(), pptr());
            return buffer;
        }

        std::size_t sput(char_type c)
        {
            return sputc(c) == traits_type::eof() ? 0 : 1;
        }

        std::size_t sput(const char_type* data, std::size_t n)
        {
            return static_cast<std::size_t>(sputn(data, n));
        }

        template<std::size_t N>
        std::size_t sput(const char_type(&data)[N])
        {
            return static_cast<std::size_t>(sputn(data, N - 1));
        }
    };

    using streambuf = basic_streambuf<>;
}
#endif