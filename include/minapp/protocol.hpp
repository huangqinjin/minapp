#ifndef MINAPP_PROTOCOL_HPP
#define MINAPP_PROTOCOL_HPP

namespace minapp
{
    enum class protocol
    {
        none,
        any,
        fixed,
        delim,
        delim_zero,
        delim_cr,
        delim_lf,
        delim_crlf,
        prefix_8,
        prefix_16,
        prefix_32,
        prefix_64,
    };

    enum class protocol_options : unsigned
    {
        do_not_consume_buffer = 1,                              // only supports protocol::fixed
        ignore_protocol_bytes = do_not_consume_buffer << 1,     // supports protocol::prefix_*
        use_little_endian = ignore_protocol_bytes << 1,         // supports protocol::prefix_*
    };

    inline unsigned to_integer(protocol_options o) { return static_cast<unsigned>(o); }

#define MINAPP_PROTOCOL_OPTIONS_OPERATOR(op) \
    inline protocol_options operator op (protocol_options lhs, protocol_options rhs)        \
        { return static_cast<protocol_options>(to_integer(lhs) op to_integer(rhs)); }       \
    inline protocol_options& operator op ## = (protocol_options& lhs, protocol_options rhs) \
        { lhs = lhs op rhs; return lhs; }

    MINAPP_PROTOCOL_OPTIONS_OPERATOR(&)
    MINAPP_PROTOCOL_OPTIONS_OPERATOR(|)
    MINAPP_PROTOCOL_OPTIONS_OPERATOR(^)

    inline protocol_options operator~(protocol_options o) { return static_cast<protocol_options>(~to_integer(o)); }
    inline bool has_options(protocol_options all_options, protocol_options options) { return (all_options & options) == options; }
}
#endif