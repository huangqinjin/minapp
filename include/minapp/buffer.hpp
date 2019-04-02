#ifndef MINAPP_BUFFER_HPP
#define MINAPP_BUFFER_HPP

#include <boost/asio/buffer.hpp>

namespace minapp
{
    class buffer : public boost::asio::mutable_buffer
    {
    protected:
        buffer() = default;
        buffer(const buffer&) = default;
        buffer& operator=(const buffer&) = default;
        ~buffer() = default;

    public:
        inline boost::asio::mutable_buffer whole();
        inline buffer& consume(std::size_t n);
    };

    class triple_buffer : public buffer
    {
        /**
         *   +----------------+----------------+--------+----------+
         *   | external input | internal input | output | reserved |
         *   +----------------+----------------+--------+----------+
         *
         *   external input: access from handler::read()
         *   internal input: commit from output in asio::[async_]read_until()
         *   output: fill by asio::[async_]read()
         */
        std::vector<char> storage;
        std::size_t external_input_size = 0;
        std::size_t internal_input_size = 0;

    public:
        using const_buffers_type = boost::asio::const_buffer;
        using mutable_buffers_type = boost::asio::mutable_buffer;

        std::size_t size() const
        {
            return storage.size();
        }

        std::size_t max_size() const
        {
            return storage.max_size();
        }

        std::size_t capacity() const
        {
            return storage.capacity();
        }

        mutable_buffers_type output_buffer()
        {
            std::size_t sz = external_input_size + internal_input_size;
            return boost::asio::buffer(storage.data() + sz, storage.size() - sz);
        }

        mutable_buffers_type prepare_output_buffer(std::size_t n)
        {
            std::size_t sz = external_input_size + internal_input_size;
            storage.resize(sz + n);
            return boost::asio::buffer(storage.data() + sz, n);
        }

        void grow_output_buffer(std::size_t n)
        {
            storage.resize(storage.size() + n);
        }

        void shrink_output_buffer(std::size_t n)
        {
            n = (std::min)(n, output_buffer().size());
            storage.resize(storage.size() - n);
        }

        mutable_buffers_type internal_input_buffer()
        {
            return boost::asio::buffer(storage.data() + external_input_size, internal_input_size);
        }

        void commit_to_internal_input(std::size_t n)
        {
            internal_input_size += (std::min)(n, output_buffer().size());
        }

        void consume_from_internal_input(std::size_t n)
        {
            if(n > internal_input_size) n = internal_input_size;
            internal_input_size -= n;
        }

        void commit_whole_internal_input()
        {
            external_input_size += internal_input_size;
            internal_input_size = 0;
        }

        mutable_buffers_type external_input_buffer()
        {
            return boost::asio::buffer(storage.data(), external_input_size);
        }

        void commit_to_external_input(std::size_t n)
        {
            if(n > internal_input_size) n = internal_input_size;
            external_input_size += n;
            internal_input_size -= n;
        }

        void consume_from_external_input(std::size_t n)
        {
            if(n > external_input_size) n = external_input_size;
            storage.erase(storage.begin(), storage.begin() + n);
            external_input_size -= n;
        }

        void consume_whole_external_input()
        {
            storage.erase(storage.begin(), storage.begin() + external_input_size);
            external_input_size = 0;
        }

        void mark_current_external_input()
        {
            mutable_buffer::operator=(external_input_buffer());
        }

        void move_to_new_external_input_segment()
        {
            mutable_buffer::operator=(external_input_buffer() + mutable_buffer::size());
        }
    };

    boost::asio::mutable_buffer buffer::whole()
    {
        auto& impl = static_cast<triple_buffer&>(*this);
        return impl.external_input_buffer();
    }

    buffer& buffer::consume(std::size_t n)
    {
        auto& impl = static_cast<triple_buffer&>(*this);
        auto whole = impl.external_input_buffer();
        auto origin = static_cast<unsigned char*>(whole.data());
        impl.consume_from_external_input(n);
        if(data() >= origin || data() < origin + whole.size())
        {
            std::size_t begin = static_cast<unsigned char*>(data()) - origin;
            std::size_t end = begin + size();

            if(n < begin) { begin -= n; }
            else if(n < end) { begin = 0; end -= n; }
            else { begin = end = 0; }

            origin = static_cast<unsigned char*>(impl.external_input_buffer().data());
            mutable_buffer::operator=({origin + begin, end - begin});
        }
        return *this;
    }
}
#endif
