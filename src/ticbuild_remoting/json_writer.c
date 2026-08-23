#include "ticbuild_remoting/json_writer.h"

#include "ticbuild_remoting/utils.h"

#include <stdio.h>

bool tb_json_append_string(tb_text_buffer* out, const char* value, char* err, size_t errcap)
{
    const unsigned char* text = (const unsigned char*)(value ? value : "");
    if(!tb_text_buffer_append_char(out, '"', err, errcap))
        return false;

    for(; *text; text++)
    {
        char escaped[8];
        const char* fragment = NULL;
        size_t length = 0;
        switch(*text)
        {
        case '"': fragment = "\\\""; length = 2; break;
        case '\\': fragment = "\\\\"; length = 2; break;
        case '\b': fragment = "\\b"; length = 2; break;
        case '\f': fragment = "\\f"; length = 2; break;
        case '\n': fragment = "\\n"; length = 2; break;
        case '\r': fragment = "\\r"; length = 2; break;
        case '\t': fragment = "\\t"; length = 2; break;
        default:
            if(*text < 0x20)
            {
                snprintf(escaped, sizeof escaped, "\\u%04x", (unsigned)*text);
                fragment = escaped;
                length = 6;
            }
            else
            {
                escaped[0] = (char)*text;
                fragment = escaped;
                length = 1;
            }
            break;
        }

        if(!tb_text_buffer_append(out, fragment, length, err, errcap))
            return false;
    }

    return tb_text_buffer_append_char(out, '"', err, errcap);
}

bool tb_json_append_int(tb_text_buffer* out, int64_t value, char* err, size_t errcap)
{
    char text[32];
    snprintf(text, sizeof text, "%lld", (long long)value);
    return tb_text_buffer_append_cstr(out, text, err, errcap);
}

bool tb_json_append_bool(tb_text_buffer* out, bool value, char* err, size_t errcap)
{
    return tb_text_buffer_append_cstr(out, value ? "true" : "false", err, errcap);
}
