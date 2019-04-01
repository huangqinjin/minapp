#ifndef MINAPP_BUFFER_HPP
#define MINAPP_BUFFER_HPP

#include <boost/asio/buffer.hpp>

namespace minapp
{
    class buffer
    {
    protected:
        buffer() = default;
        buffer(const buffer&) = default;
        buffer& operator=(const buffer&) = default;
        ~buffer() = default;

    public:
        inline buffer& consume(std::size_t n);
        inline void* data() const;
        inline std::size_t size() const;
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
    };

    buffer& buffer::consume(std::size_t n)
    {
        auto& impl = (triple_buffer&)(*this);
        impl.consume_from_external_input(n);
        return *this;
    }

    void* buffer::data() const
    {
        auto& impl = (triple_buffer&)(*this);
        return impl.external_input_buffer().data();
    }

    std::size_t buffer::size() const
    {
        auto& impl = (triple_buffer&)(*this);
        return impl.external_input_buffer().size();
    }

}
#endif
