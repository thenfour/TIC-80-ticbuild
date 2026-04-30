#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ticbuild_remoting/fps.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct tb_text_buffer
    {
        char* ptr;
        size_t len;
        size_t cap;
        size_t limit;
    } tb_text_buffer;

    typedef struct
    {
        const char* ptr;
        size_t len;
    } tb_slice;

    typedef enum
    {
        TB_ARG_INT,
        TB_ARG_NUMBER,
        TB_ARG_STR,
        TB_ARG_BYTES,
    } tb_arg_type;

    typedef struct
    {
        tb_arg_type type;
        union
        {
            int64_t i;
            double n;
            tb_slice s;
            struct
            {
                const uint8_t* ptr;
                size_t len;
            } b;
        } v;
    } tb_arg;

    void tb_format_ms10(char* out, size_t cap, uint32_t ms10);
    void tb_format_ms10_value(char* out, size_t cap, uint32_t ms10);
    void tb_format_kb1(char* out, size_t cap, uint64_t bytes);
    void tb_format_kc1(char* out, size_t cap, uint64_t cycles);
    void tb_trim_trailing_zeros(char* s);
    void tb_format_fps(char* out, size_t cap, const tb_fps_tracker* fps);

    void tb_set_err(char* err, size_t cap, const char* msg);
    void tb_text_buffer_init(tb_text_buffer* buf, size_t limit);
    void tb_text_buffer_dispose(tb_text_buffer* buf);
    const char* tb_text_buffer_data(const tb_text_buffer* buf);
    bool tb_text_buffer_append(tb_text_buffer* buf, const char* s, size_t n, char* err, size_t errcap);
    bool tb_text_buffer_append_char(tb_text_buffer* buf, char c, char* err, size_t errcap);
    bool tb_text_buffer_append_cstr(tb_text_buffer* buf, const char* s, char* err, size_t errcap);
    bool tb_text_buffer_append_escaped(tb_text_buffer* buf, const char* s, size_t n, char* err, size_t errcap);
    bool tb_text_buffer_append_quoted(tb_text_buffer* buf, const char* s, size_t n, char* err, size_t errcap);
    bool tb_is_ascii_print_or_space(char c);
    bool tb_is_ascii_only(const char* s, size_t n);

    void tb_trim_whitespace(const char** sp, size_t* n);
    void tb_skip_whitespace(const char** sp, size_t* n);
    bool tb_hex_nibble(char c, uint8_t* out);
    bool tb_parse_int(tb_slice tok, int64_t* out);
    bool tb_parse_number(tb_slice tok, tb_arg* out);
    bool tb_parse_quoted(const char* s, size_t n, char** out, size_t* outlen, char* err, size_t errcap);
    void tb_free_args(tb_arg* args, size_t argc);
    bool tb_next_token(const char** sp, size_t* n, tb_slice* tok, char* err, size_t errcap);
    size_t tb_escape_string(const char* s, size_t n, char* out, size_t outcap);
    size_t tb_escape_string_len(const char* s, size_t n);

#ifdef __cplusplus
}
#endif
