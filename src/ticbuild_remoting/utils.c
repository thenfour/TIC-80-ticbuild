#include "ticbuild_remoting/utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tb_format_ms10(char* out, size_t cap, uint32_t ms10)
{
    if(!out || cap == 0) return;
    snprintf(out, cap, "%u.%ums", (unsigned)(ms10 / 10), (unsigned)(ms10 % 10));
}

void tb_format_ms10_value(char* out, size_t cap, uint32_t ms10)
{
    if(!out || cap == 0) return;
    snprintf(out, cap, "%u.%u", (unsigned)(ms10 / 10), (unsigned)(ms10 % 10));
}

void tb_format_kb1(char* out, size_t cap, uint64_t bytes)
{
    if(!out || cap == 0) return;
    uint64_t kb10 = (bytes * 10ULL + 512ULL) / 1024ULL;
    unsigned long long whole = (unsigned long long)(kb10 / 10ULL);
    unsigned long long frac = (unsigned long long)(kb10 % 10ULL);
    snprintf(out, cap, "%llu.%llu", whole, frac);
}

void tb_trim_trailing_zeros(char* s)
{
    if(!s) return;

    char* dot = strchr(s, '.');
    if(!dot) return;

    char* end = s + strlen(s) - 1;
    while(end > dot && *end == '0')
    {
        *end = '\0';
        --end;
    }

    if(end == dot)
        *end = '\0';
}

void tb_format_fps(char* out, size_t cap, const tb_fps_tracker* fps)
{
    if(!out || cap == 0) return;

    double v = 0.0;
    if(fps && fps->count > 0 && fps->sum_dt > 0 && fps->freq > 0)
        v = ((double)fps->count * (double)fps->freq) / (double)fps->sum_dt;

    snprintf(out, cap, "%.2f", v);
    tb_trim_trailing_zeros(out);
}

void tb_set_err(char* err, size_t cap, const char* msg)
{
    if(!err || cap == 0) return;
    if(!msg) msg = "error";
    strncpy(err, msg, cap - 1);
    err[cap - 1] = '\0';
}

bool tb_is_ascii_print_or_space(char c)
{
    unsigned char uc = (unsigned char)c;
    if(uc == '\t' || uc == ' ') return true;
    if(uc >= 0x21 && uc <= 0x7E) return true;
    return false;
}

bool tb_is_ascii_only(const char* s, size_t n)
{
    for(size_t i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if(c > 0x7F) return false;
        if(c == 0) return false;
    }
    return true;
}

void tb_trim_whitespace(const char** sp, size_t* n)
{
    const char* s = *sp;
    size_t len = *n;

    while(len && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r' || s[0] == '\n'))
    {
        s++;
        len--;
    }

    while(len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
        len--;

    *sp = s;
    *n = len;
}

void tb_skip_whitespace(const char** sp, size_t* n)
{
    const char* s = *sp;
    size_t len = *n;
    while(len && (s[0] == ' ' || s[0] == '\t'))
    {
        s++;
        len--;
    }
    *sp = s;
    *n = len;
}

bool tb_hex_nibble(char c, uint8_t* out)
{
    if(c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return true; }
    if(c >= 'a' && c <= 'f') { *out = (uint8_t)(10 + (c - 'a')); return true; }
    if(c >= 'A' && c <= 'F') { *out = (uint8_t)(10 + (c - 'A')); return true; }
    return false;
}

bool tb_parse_int(tb_slice tok, int64_t* out)
{
    if(tok.len == 0) return false;

    if(tok.len >= 3 && tok.ptr[0] == '0' && (tok.ptr[1] == 'x' || tok.ptr[1] == 'X'))
    {
        int64_t v = 0;
        for(size_t i = 2; i < tok.len; i++)
        {
            char c = tok.ptr[i];
            uint8_t n;
            if (!tb_hex_nibble(c, &n)) {
                return false;
            }
            v = (v << 4) | n;
        }
        *out = v;
        return true;
    }

    int64_t v = 0;
    for(size_t i = 0; i < tok.len; i++)
    {
        char c = tok.ptr[i];
        if(c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    *out = v;
    return true;
}

bool tb_parse_quoted(const char* s, size_t n, char** out, size_t* outlen, char* err, size_t errcap)
{
    if(n < 2 || s[0] != '"' || s[n - 1] != '"')
    {
        tb_set_err(err, errcap, "invalid string literal");
        return false;
    }

    char* buf = (char*)malloc(n);
    if(!buf)
    {
        tb_set_err(err, errcap, "out of memory");
        return false;
    }

    size_t j = 0;
    for(size_t i = 1; i + 1 < n; i++)
    {
        char c = s[i];
        if(c == '\\')
        {
            if(i + 1 >= n - 1) { free(buf); tb_set_err(err, errcap, "invalid escape"); return false; }
            char e = s[++i];
            if(e == '\\' || e == '"') buf[j++] = e;
            else { free(buf); tb_set_err(err, errcap, "unsupported escape"); return false; }
        }
        else
        {
            if(!tb_is_ascii_print_or_space(c)) { free(buf); tb_set_err(err, errcap, "non-ascii char in string"); return false; }
            buf[j++] = c;
        }
    }

    buf[j] = '\0';
    *out = buf;
    *outlen = j;
    return true;
}

void tb_free_args(tb_arg* args, size_t argc)
{
    for(size_t i = 0; i < argc; i++)
    {
        if(args[i].type == TB_ARG_STR)
        {
            free((void*)args[i].v.s.ptr);
        }
    }
}

bool tb_next_token(const char** sp, size_t* n, tb_slice* tok, char* err, size_t errcap)
{
    tb_skip_whitespace(sp, n);
    if(*n == 0) return false;

    const char* s = *sp;
    size_t len = *n;

    if(s[0] == '"')
    {
        size_t i = 1;
        for(; i < len; i++)
        {
            char c = s[i];
            if(c == '\\')
            {
                i++;
                continue;
            }
            if(c == '"')
            {
                i++;
                break;
            }
        }
        if(i > len) { tb_set_err(err, errcap, "unterminated string"); return false; }
        *tok = (tb_slice){s, i};
        *sp = s + i;
        *n = len - i;
        return true;
    }

    if(s[0] == '<')
    {
        size_t i = 1;
        for(; i < len; i++)
        {
            if(s[i] == '>') { i++; break; }
        }
        if(i > len) { tb_set_err(err, errcap, "unterminated binary"); return false; }
        *tok = (tb_slice){s, i};
        *sp = s + i;
        *n = len - i;
        return true;
    }

    size_t i = 0;
    while(i < len && s[i] != ' ' && s[i] != '\t')
        i++;

    *tok = (tb_slice){s, i};
    *sp = s + i;
    *n = len - i;
    return true;
}

size_t tb_escape_string(const char* s, size_t n, char* out, size_t outcap)
{
    size_t j = 0;
    if(outcap == 0) return 0;

    for(size_t i = 0; i < n; i++)
    {
        char c = s[i];
        if((unsigned char)c > 0x7F) c = '?';
        if(c == '\\' || c == '"')
        {
            if(j + 2 >= outcap) break;
            out[j++] = '\\';
            out[j++] = c;
        }
        else
        {
            if(!tb_is_ascii_print_or_space(c)) c = '?';
            if(j + 1 >= outcap) break;
            out[j++] = c;
        }
    }

    out[j] = '\0';
    return j;
}

size_t tb_escape_string_len(const char* s, size_t n)
{
    size_t j = 0;

    for(size_t i = 0; i < n; i++)
    {
        char c = s[i];
        if((unsigned char)c > 0x7F) c = '?';
        if(c == '\\' || c == '"')
            j += 2;
        else
            j += 1;
    }

    return j;
}
