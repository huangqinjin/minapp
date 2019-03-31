#ifndef MINAPP_MEMORY_PRINTER_HPP
#define MINAPP_MEMORY_PRINTER_HPP

#include <ostream>

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
    class memory_printer
    {
    public:
        struct options
        {
            MINAPP_DEFAULT_MEMBER_INITIALIZER_FOR_NESTED_CLASS(options)
            unsigned bytes_per_line = 16;
            bool uppercase = false;
        };

        virtual void putc(char c) = 0;

        virtual char printable(char c)
        {
            const auto sc = static_cast<signed char>(c);
            return sc > 0x20 ? c : '.';
        }

        void operator()(const void* memory, std::size_t size, const options& o = {})
        {
            print(memory, size, o);
        }

        void print(const void* memory, std::size_t size, const options& o = {})
        {
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
                putc('|'); putc(' ');
                for (unsigned j = 0; j < o.bytes_per_line; ++j)
                {
                    char c = *p++;
                    putc(printable(c));
                }
                putc('\n');
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
                putc('|'); putc(' ');
                for (unsigned j = 0; j < size; ++j)
                {
                    char c = *p++;
                    putc(printable(c));
                }
                putc('\n');
            }
        }
    };

    class streambuf_memory_printer : public memory_printer
    {
        std::streambuf* buf;

    public:
        explicit streambuf_memory_printer(std::streambuf* buf)
            : buf(buf) {}

        explicit streambuf_memory_printer(std::ostream& os)
            : streambuf_memory_printer(os.rdbuf()) {}

        void putc(char c) override
        {
            buf->sputc(c);
        }
    };
}
#endif