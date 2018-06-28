#ifndef MINAPP_MEMORY_PRINTER_HPP
#define MINAPP_MEMORY_PRINTER_HPP

#include <iostream>

namespace minapp
{
    template<bool UpperCase = false, char Unprintable = '.'>
    class memory_printer
    {
        const unsigned bytes_per_line;

    public:
        explicit memory_printer(unsigned bytes_per_line = 16)
            : bytes_per_line(bytes_per_line)
        {}

        virtual void putc(char c) = 0;

        virtual bool printable(char c)
        {
            signed char sc = c;
            return sc > 0x20;
        }

        void operator()(const void* memory, std::size_t size)
        {
            print(memory, size);
        }

        void print(const void* memory, std::size_t size)
        {
            const char* table = UpperCase ? "0123456789ABCDEF" : "0123456789abcdef";
            auto hexer = [table]
            (char c, char& high, char& low)
            {
                high = table[((unsigned char)c) >> 4];
                low = table[c & 0x0f];
            };

            const unsigned char* p = (const unsigned char*) memory;
            for (unsigned i = 0; i < size / bytes_per_line; ++i)
            {
                putc('|'); putc(' ');
                for (unsigned j = 0; j < bytes_per_line; ++j)
                {
                    char high, low;
                    hexer(*(p + j), high, low);
                    putc(high); putc(low); putc(' ');
                }
                putc('|'); putc(' ');
                for (unsigned j = 0; j < bytes_per_line; ++j)
                {
                    char c = *p++;
                    putc(printable(c) ? c : Unprintable);
                }
                putc('\n');
            }

            size %= bytes_per_line;
            if (size > 0)
            {
                putc('|'); putc(' ');
                for (unsigned j = 0; j < size; ++j)
                {
                    char high, low;
                    hexer(*(p + j), high, low);
                    putc(high); putc(low); putc(' ');
                }
                for (unsigned j = 0; j < 3 * (bytes_per_line - size); ++j)
                {
                    putc(' ');
                }
                putc('|'); putc(' ');
                for (unsigned j = 0; j < size; ++j)
                {
                    char c = *p++;
                    putc(printable(c) ? c : Unprintable);
                }
                putc('\n');
            }
        }
    };

    template<bool UpperCase = false, char Unprintable = '.'>
    class streambuf_memory_printer : public memory_printer<UpperCase, Unprintable>
    {
        std::streambuf* buf;

    public:
        explicit streambuf_memory_printer(std::streambuf* buf,
            unsigned bytes_per_line = 16)
            : memory_printer<UpperCase, Unprintable>(bytes_per_line), buf(buf)
        {}

        explicit streambuf_memory_printer(std::ostream& os, unsigned bytes_per_line = 16)
            : streambuf_memory_printer(os.rdbuf(), bytes_per_line)
        {}

        void putc(char c) override
        {
            buf->sputc(c);
        }
    };
}
#endif