#ifndef MINAPP_HEXDUMP_HPP
#define MINAPP_HEXDUMP_HPP

#include <ostream>      // basic_ostream, basic_streambuf
#include <cstdio>       // fputc, FILE
#include <cwchar>       // fputwc

#ifdef _MSC_VER
#  define MINAPP_DEFAULT_MEMBER_INITIALIZER_FOR_NESTED_CLASS(C)
#else
// a bug of both gcc and clang
// https://stackoverflow.com/questions/53408962/try-to-understand-compiler-error-message-default-member-initializer-required-be
// https://stackoverflow.com/questions/17430377/error-when-using-in-class-initialization-of-non-static-data-member-and-nested-cl
#  define MINAPP_DEFAULT_MEMBER_INITIALIZER_FOR_NESTED_CLASS(C) C() {}
#endif

namespace minapp
{
    class hexdump
    {
    public:
        struct options
        {
            MINAPP_DEFAULT_MEMBER_INITIALIZER_FOR_NESTED_CLASS(options)
            bool uppercase = false;
            char min_printable = 0x21;
            char max_printable = 0x7f;
            char placeholder = '.';
            unsigned bytes_per_line = 16;
        };

        using putf = void(char c, void* buf);

        hexdump(putf* put, void* buf) : put(put), buf(buf) {}

        // For wide output, file should be in wide-oriented mode set via fwide(file, 1)
        // or _setmode(_fileno(file), _O_U8TEXT /* or _O_U16TEXT */) on Windows.
        explicit hexdump(std::FILE* file, bool wide = false)
            : hexdump(wide ? (putf*)&std::fputwc : (putf*)&std::fputc, file) {}

        template<typename CharT>
        explicit hexdump(std::basic_streambuf<CharT>* buf)
            : hexdump([](char c, void* buf) {
                static_cast<std::basic_streambuf<CharT>*>(buf)->sputc(c);
            }, buf) {}

        template<typename CharT>
        explicit hexdump(std::basic_ostream<CharT>& os)
            : hexdump(os.rdbuf()) {}

        void operator()(const void* memory, std::size_t size, const options& o = {})
        {
            auto putc = [this](char c) { put(c, buf); };
            auto printable = [&o](char c){ return c >= o.min_printable && c <= o.max_printable ? c : o.placeholder;  };
            const char* table = o.uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
            auto hexer = [table](char c, char& high, char& low)
            {
                const auto uc = static_cast<unsigned char>(c);
                high = table[uc >> 4];
                low = table[uc & 0x0f];
            };

            auto p = static_cast<const unsigned char*>(memory);
            for (unsigned i = 0; i < size / o.bytes_per_line; ++i)
            {
                putc('|'); putc(' ');
                for (unsigned j = 0; j < o.bytes_per_line; ++j)
                {
                    char high, low;
                    hexer(*(p + j), high, low);
                    putc(high); putc(low); putc(' ');
                }
                putc('|');
                for (unsigned j = 0; j < o.bytes_per_line; ++j)
                {
                    char c = *p++;
                    putc(printable(c));
                }
                putc('|'); putc('\n');
            }

            size %= o.bytes_per_line;
            if (size > 0)
            {
                putc('|'); putc(' ');
                for (unsigned j = 0; j < size; ++j)
                {
                    char high, low;
                    hexer(*(p + j), high, low);
                    putc(high); putc(low); putc(' ');
                }
                for (unsigned j = 0; j < 3 * (o.bytes_per_line - size); ++j)
                {
                    putc(' ');
                }
                putc('|');
                for (unsigned j = 0; j < size; ++j)
                {
                    char c = *p++;
                    putc(printable(c));
                }
                for (unsigned j = 0; j < (o.bytes_per_line - size); ++j)
                {
                    putc(' ');
                }
                putc('|'); putc('\n');
            }
        }

    private:
        putf* const put;
        void* const buf;
    };
}
#endif
