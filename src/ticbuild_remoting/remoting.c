#include "ticbuild_remoting/remoting.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__) || !(defined(_WIN32) || defined(__TIC_WINDOWS__))

// for wasm builds or non-windows builds, stub.

struct TicbuildRemoting { int unused; };

TicbuildRemoting* ticbuild_remoting_create(int port, const char* session_dir, bool global_disco, const ticbuild_remoting_callbacks* callbacks)
{
    (void)port; (void)session_dir; (void)global_disco; (void)callbacks;
    return NULL;
}

void ticbuild_remoting_close(TicbuildRemoting* ctx) { (void)ctx; }
void ticbuild_remoting_tick(TicbuildRemoting* ctx) { (void)ctx; }

void ticbuild_remoting_on_frame(TicbuildRemoting* ctx, uint64_t counter, uint64_t freq) { (void)ctx; (void)counter; (void)freq; }
int ticbuild_remoting_get_fps(const TicbuildRemoting* ctx) { (void)ctx; return 0; }
void ticbuild_remoting_set_user_time_ms10(TicbuildRemoting* ctx, uint32_t tic_ms10, uint32_t scn_ms10, uint32_t bdr_ms10, uint32_t total_ms10)
{
    (void)ctx; (void)tic_ms10; (void)scn_ms10; (void)bdr_ms10; (void)total_ms10;
}
void ticbuild_remoting_set_lua_perf(TicbuildRemoting* ctx, uint64_t tic_cycles, uint64_t scn_cycles, uint64_t bdr_cycles, uint64_t lua_gc_mem_bytes)
{
    (void)ctx; (void)tic_cycles; (void)scn_cycles; (void)bdr_cycles; (void)lua_gc_mem_bytes;
}
void ticbuild_remoting_get_title_info(const TicbuildRemoting* ctx, char* out, size_t outcap) { (void)ctx; if(out && outcap) out[0] = '\0'; }
bool ticbuild_remoting_take_title_dirty(TicbuildRemoting* ctx) { (void)ctx; return false; }

#else

#include "ticbuild_remoting/discovery.h"


// todo: a x-platform abstraction? sdl?
# include <winsock2.h>
# include <ws2tcpip.h>
# pragma comment(lib, "Ws2_32.lib")

# include "ticbuild_remoting/fps.h"
# include "ticbuild_remoting/utils.h"
typedef SOCKET tb_socket;
# define TB_INVALID_SOCKET INVALID_SOCKET
# define tb_close_socket closesocket
static int tb_last_socket_error(void) { return WSAGetLastError(); }
static bool tb_would_block(int err) { return err == WSAEWOULDBLOCK; }

/* just make these huge; there's not much reason in a windows x64 env to restrict. */
enum { TB_INBUF_LIMIT = 1024 * 1024 * 10 /* 10 MB */ };
enum { TB_OUTBUF_LIMIT = 1024 * 1024 * 10 };
enum { TB_LINE_LIMIT = 1024 * 1024 * 10 };
enum { TB_TEXT_LINE_LIMIT = TB_OUTBUF_LIMIT < TB_LINE_LIMIT ? TB_OUTBUF_LIMIT : TB_LINE_LIMIT };
enum { TB_RESPONSE_TEXT_HEADROOM = 64 };
enum { TB_BYTES_RESPONSE_HEADROOM = 68 };
enum { TB_RESPONSE_TEXT_LIMIT = TB_TEXT_LINE_LIMIT > TB_RESPONSE_TEXT_HEADROOM ? (TB_TEXT_LINE_LIMIT - TB_RESPONSE_TEXT_HEADROOM) : TB_TEXT_LINE_LIMIT };
enum { TB_PEEK_LIMIT = TB_TEXT_LINE_LIMIT > TB_BYTES_RESPONSE_HEADROOM ? (TB_TEXT_LINE_LIMIT - TB_BYTES_RESPONSE_HEADROOM) / 3 : 0 };
enum { TB_MAX_CLIENTS = 10 };

typedef struct
{
    tb_socket sock;

    char* inbuf;
    size_t inlen;

    char* outbuf;
    size_t outlen;
    size_t outpos;
} tb_client;

struct TicbuildRemoting
{
    int port;
    ticbuild_remoting_callbacks cb;

    char* discovery_dir;
    bool global_disco;

    // FPS tracking (time-window moving average)
    tb_fps_tracker fps;

    // Title/status tracking
    bool title_dirty;
    int last_client_count;
    char last_listen_err[128];

    // Per-frame user callback timing (0.1ms fixed units)
    uint32_t user_tic_ms10;
    uint32_t user_scn_ms10;
    uint32_t user_bdr_ms10;
    uint32_t user_total_ms10;

    // Per-frame Lua perf metrics
    uint64_t user_tic_cycles;
    uint64_t user_scn_cycles;
    uint64_t user_bdr_cycles;
    uint64_t lua_gc_mem_bytes;

    bool wsa_started;

    tb_socket listen_sock;

    tb_client clients[TB_MAX_CLIENTS];
    int client_count;

    uint8_t* tmpbytes;
    size_t tmpbytes_cap;
};

static void tb_mark_title_dirty(TicbuildRemoting* ctx)
{
    if(ctx) ctx->title_dirty = true;
}


void ticbuild_remoting_on_frame(TicbuildRemoting* ctx, uint64_t counter, uint64_t freq)
{
    if(!ctx) return;

    if(tb_fps_on_frame(&ctx->fps, counter, freq))
        tb_mark_title_dirty(ctx);
}

int ticbuild_remoting_get_fps(const TicbuildRemoting* ctx)
{
    return ctx ? tb_fps_get(&ctx->fps) : 0;
}

void ticbuild_remoting_set_user_time_ms10(TicbuildRemoting* ctx, uint32_t tic_ms10, uint32_t scn_ms10, uint32_t bdr_ms10, uint32_t total_ms10)
{
    if(!ctx) return;

    if(ctx->user_tic_ms10 != tic_ms10 ||
       ctx->user_scn_ms10 != scn_ms10 ||
       ctx->user_bdr_ms10 != bdr_ms10 ||
       ctx->user_total_ms10 != total_ms10)
    {
        ctx->user_tic_ms10 = tic_ms10;
        ctx->user_scn_ms10 = scn_ms10;
        ctx->user_bdr_ms10 = bdr_ms10;
        ctx->user_total_ms10 = total_ms10;
        tb_mark_title_dirty(ctx);
    }
}

void ticbuild_remoting_set_lua_perf(TicbuildRemoting* ctx, uint64_t tic_cycles, uint64_t scn_cycles, uint64_t bdr_cycles, uint64_t lua_gc_mem_bytes)
{
    if(!ctx) return;

    if(ctx->user_tic_cycles != tic_cycles ||
       ctx->user_scn_cycles != scn_cycles ||
       ctx->user_bdr_cycles != bdr_cycles ||
       ctx->lua_gc_mem_bytes != lua_gc_mem_bytes)
    {
        ctx->user_tic_cycles = tic_cycles;
        ctx->user_scn_cycles = scn_cycles;
        ctx->user_bdr_cycles = bdr_cycles;
        ctx->lua_gc_mem_bytes = lua_gc_mem_bytes;
        tb_mark_title_dirty(ctx);
    }
}

void ticbuild_remoting_get_title_info(const TicbuildRemoting* ctx, char* out, size_t outcap)
{
    if(!out || outcap == 0) return;
    out[0] = '\0';
    if(!ctx) return;

    const char* listen_state = NULL;
    char listen_buf[256];

    if(ctx->listen_sock != TB_INVALID_SOCKET)
    {
        snprintf(listen_buf, sizeof listen_buf, "listening on 127.0.0.1:%d (%d clients)", ctx->port, ctx->client_count);
        listen_state = listen_buf;
    }
    else if(ctx->last_listen_err[0])
    {
        snprintf(listen_buf, sizeof listen_buf, "remoting error: %s", ctx->last_listen_err);
        listen_state = listen_buf;
    }
    else
    {
        listen_state = "remoting not listening";
    }

    char ticbuf[32], scnbuf[32], totbuf[32];
    char luabuf[32];
    uint64_t scn_bdr_cycles = ctx->user_scn_cycles + ctx->user_bdr_cycles;

    tb_format_kc1(ticbuf, sizeof ticbuf, ctx->user_tic_cycles);
    tb_format_kc1(scnbuf, sizeof scnbuf, scn_bdr_cycles);
    tb_format_kc1(totbuf, sizeof totbuf, ctx->user_tic_cycles + scn_bdr_cycles);
    tb_format_kb1(luabuf, sizeof luabuf, ctx->lua_gc_mem_bytes);

    snprintf(out, outcap,
        "FPS: %d | TIC %sk SCN+BDR %sk TOT %sk | LUA %sKB | %s",
        tb_fps_get(&ctx->fps),
        ticbuf, scnbuf, totbuf,
        luabuf,
        listen_state);
}

bool ticbuild_remoting_take_title_dirty(TicbuildRemoting* ctx)
{
    if(!ctx) return false;
    bool v = ctx->title_dirty;
    ctx->title_dirty = false;
    return v;
}

// parses binary literal of form <hex...>
// - whitespace ignored
// - full bytes required (<f> is invalid, must be <0f>). disambigation because while <f> is obvious,
//   <1234f> is not clear. is that 12 34 f0 or 12 34 0f?
//   <14f> probably most natural to interpret this as 14 f0, but that is inconsistent with <f>.
//   it could also be 01 4f.. better to just require 2-digit bytes.
static bool tb_parse_bytes(const char* s, size_t n, uint8_t** out, size_t* outlen, TicbuildRemoting* ctx, char* err, size_t errcap)
{
    // s points at '<', ends with '>'
    if(n < 2 || s[0] != '<' || s[n - 1] != '>')
    {
        tb_set_err(err, errcap, "invalid binary literal");
        return false;
    }

    // collect hex digits only, ignore whitespace
    char* hex = (char*)malloc(n);
    if(!hex)
    {
        tb_set_err(err, errcap, "out of memory");
        return false;
    }

    size_t hn = 0;
    for(size_t i = 1; i + 1 < n; i++)
    {
        char c = s[i];
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if(!isxdigit((unsigned char)c)) { free(hex); tb_set_err(err, errcap, "binary contains non-hex"); return false; }
        hex[hn++] = c;
    }

    if(hn % 2 != 0)
    {
        free(hex);
        tb_set_err(err, errcap, "binary hex digit count must be even");
        return false;
    }

    size_t bytes = hn / 2;
    if(bytes > TB_PEEK_LIMIT)
    {
        free(hex);
        tb_set_err(err, errcap, "binary too large");
        return false;
    }

    if(ctx->tmpbytes_cap < bytes)
    {
        uint8_t* nb = (uint8_t*)realloc(ctx->tmpbytes, bytes);
        if(!nb)
        {
            free(hex);
            tb_set_err(err, errcap, "out of memory");
            return false;
        }
        ctx->tmpbytes = nb;
        ctx->tmpbytes_cap = bytes;
    }

    for(size_t i = 0; i < bytes; i++)
    {
        uint8_t hi, lo;
        if(!tb_hex_nibble(hex[i * 2], &hi) || !tb_hex_nibble(hex[i * 2 + 1], &lo))
        {
            free(hex);
            tb_set_err(err, errcap, "invalid hex");
            return false;
        }
        ctx->tmpbytes[i] = (uint8_t)((hi << 4) | lo);
    }

    free(hex);
    *out = ctx->tmpbytes;
    *outlen = bytes;
    return true;
}

static void tb_queue_output(tb_client* client, const char* s, size_t n)
{
    if(n == 0) return;

    if(client->outlen + n > TB_OUTBUF_LIMIT)
    {
        // Drop output if client can't keep up.
        return;
    }

    char* nb = (char*)realloc(client->outbuf, client->outlen + n);
    if(!nb) return;

    memcpy(nb + client->outlen, s, n);
    client->outbuf = nb;
    client->outlen += n;
}

static void tb_send_response_str(tb_client* client, int64_t id, bool ok, const char* data)
{
    char prefix[64];

    if(ok)
    {
        int plen = snprintf(prefix, sizeof prefix, "%lld OK", (long long)id);
        if(plen < 0) return;

        size_t prefix_len = (size_t)plen;
        size_t data_len = (data && data[0]) ? strlen(data) : 0;
        size_t line_len = prefix_len + (data_len ? (1 + data_len) : 0) + 1;

        char* line = (char*)malloc(line_len + 1);
        if(!line)
        {
            char fallback[64];
            int flen = snprintf(fallback, sizeof fallback, "%lld ERR \"out of memory\"\n", (long long)id);
            if(flen > 0)
                tb_queue_output(client, fallback, (size_t)flen);
            return;
        }

        size_t pos = 0;
        memcpy(line + pos, prefix, prefix_len);
        pos += prefix_len;

        if(data_len)
        {
            line[pos++] = ' ';
            memcpy(line + pos, data, data_len);
            pos += data_len;
        }

        line[pos++] = '\n';
        line[pos] = '\0';

        tb_queue_output(client, line, pos);
        free(line);
    }
    else
    {
        const char* raw = data ? data : "error";
        size_t raw_len = strlen(raw);
        size_t esc_len = tb_escape_string_len(raw, raw_len);
        int plen = snprintf(prefix, sizeof prefix, "%lld ERR \"", (long long)id);
        if(plen < 0) return;

        size_t prefix_len = (size_t)plen;
        size_t line_len = prefix_len + esc_len + 2;
        char* line = (char*)malloc(line_len + 1);
        if(!line)
        {
            char fallback[64];
            int flen = snprintf(fallback, sizeof fallback, "%lld ERR \"out of memory\"\n", (long long)id);
            if(flen > 0)
                tb_queue_output(client, fallback, (size_t)flen);
            return;
        }

        memcpy(line, prefix, prefix_len);
        size_t pos = prefix_len;
        pos += tb_escape_string(raw, raw_len, line + pos, esc_len + 1);
        line[pos++] = '"';
        line[pos++] = '\n';
        line[pos] = '\0';

        tb_queue_output(client, line, pos);
        free(line);
    }
}

static void tb_send_response_bytes(tb_client* client, int64_t id, const uint8_t* bytes, size_t n)
{
    // Format: <id> OK <aa bb cc>
    // Worst-case size: 1 token per byte + spaces.
    size_t cap = 64 + n * 3 + 4;
    if(cap > TB_TEXT_LINE_LIMIT)
    {
        tb_send_response_str(client, id, false, "result too large");
        return;
    }

    char* line = (char*)malloc(cap);
    if(!line) { tb_send_response_str(client, id, false, "out of memory"); return; }

    size_t pos = 0;
    pos += (size_t)snprintf(line + pos, cap - pos, "%lld OK <", (long long)id);

    for(size_t i = 0; i < n; i++)
    {
        if(i) line[pos++] = ' ';
        pos += (size_t)snprintf(line + pos, cap - pos, "%02x", (unsigned)bytes[i]);
    }

    pos += (size_t)snprintf(line + pos, cap - pos, ">\n");

    tb_queue_output(client, line, pos);
    free(line);
}

static void tb_text_response_init(tb_text_buffer* buf)
{
    tb_text_buffer_init(buf, TB_RESPONSE_TEXT_LIMIT);
}


static bool tb_set_nonblocking(tb_socket s)
{
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

static bool tb_socket_init(TicbuildRemoting* ctx, char* err, size_t errcap)
{
    if(!ctx->wsa_started)
    {
        WSADATA wsa;
        int r = WSAStartup(MAKEWORD(2, 2), &wsa);
        if(r != 0)
        {
            tb_set_err(err, errcap, "WSAStartup failed");
            return false;
        }
        ctx->wsa_started = true;
    }

    if(ctx->listen_sock != TB_INVALID_SOCKET)
        return true;

    tb_socket s = (tb_socket)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(s == TB_INVALID_SOCKET)
    {
        tb_set_err(err, errcap, "socket() failed");
        return false;
    }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    if(!tb_set_nonblocking(s))
    {
        tb_close_socket(s);
        tb_set_err(err, errcap, "failed to set nonblocking");
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)ctx->port);

    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if(bind(s, (struct sockaddr*)&addr, sizeof addr) != 0)
    {
        tb_close_socket(s);
        tb_set_err(err, errcap, "bind() failed (port in use?)");
        return false;
    }

    if(listen(s, TB_MAX_CLIENTS) != 0)
    {
        tb_close_socket(s);
        tb_set_err(err, errcap, "listen() failed");
        return false;
    }

    ctx->listen_sock = s;

#if defined(_WIN32) || defined(__TIC_WINDOWS__)
    // Best-effort discovery file creation; don't block remoting on failure.
    tb_discovery_start(ctx->port, ctx->discovery_dir, ctx->global_disco, NULL, 0);
#endif
    return true;
}

static void tb_disconnect_client(TicbuildRemoting* ctx, int index)
{
    if(index < 0 || index >= TB_MAX_CLIENTS) return;

    tb_client* client = &ctx->clients[index];
    if(client->sock != TB_INVALID_SOCKET)
    {
        tb_close_socket(client->sock);
        client->sock = TB_INVALID_SOCKET;
        if(ctx->client_count > 0) ctx->client_count--;
    }

    tb_mark_title_dirty(ctx);

    client->inlen = 0;
    client->outlen = 0;
    client->outpos = 0;
    free(client->inbuf);
    free(client->outbuf);
    client->inbuf = NULL;
    client->outbuf = NULL;
}

static void tb_accept_client(TicbuildRemoting* ctx)
{
    struct sockaddr_in addr;
    int alen = (int)sizeof addr;

    for(;;)
    {
        tb_socket c = (tb_socket)accept(ctx->listen_sock, (struct sockaddr*)&addr, &alen);
        if(c == TB_INVALID_SOCKET)
        {
            int err = tb_last_socket_error();
            if(tb_would_block(err)) return;
            return;
        }

        int slot = -1;
        for(int i = 0; i < TB_MAX_CLIENTS; i++)
        {
            if(ctx->clients[i].sock == TB_INVALID_SOCKET)
            {
                slot = i;
                break;
            }
        }

        if(slot < 0)
        {
            tb_close_socket(c);
            continue;
        }

        (void)tb_set_nonblocking(c);
        ctx->clients[slot].sock = c;
        ctx->clients[slot].inlen = 0;
        ctx->clients[slot].outlen = 0;
        ctx->clients[slot].outpos = 0;
        ctx->client_count++;
        tb_mark_title_dirty(ctx);
    }
}

static bool tb_read_client(TicbuildRemoting* ctx, int index)
{
    if(index < 0 || index >= TB_MAX_CLIENTS) return false;

    tb_client* client = &ctx->clients[index];
    if(client->sock == TB_INVALID_SOCKET) return false;

    if(client->inbuf == NULL)
    {
        client->inbuf = (char*)malloc(TB_INBUF_LIMIT);
        if(!client->inbuf) return false;
    }

    for(;;)
    {
        if(client->inlen >= TB_INBUF_LIMIT)
        {
            tb_disconnect_client(ctx, index);
            return false;
        }

        int r = (int)recv(client->sock, client->inbuf + client->inlen, (int)(TB_INBUF_LIMIT - client->inlen), 0);
        if(r == 0)
        {
            tb_disconnect_client(ctx, index);
            return false;
        }
        if(r < 0)
        {
            int err = tb_last_socket_error();
            if(tb_would_block(err)) break;
            tb_disconnect_client(ctx, index);
            return false;
        }

        client->inlen += (size_t)r;
    }

    return true;
}

static void tb_flush_output(TicbuildRemoting* ctx, int index)
{
    if(index < 0 || index >= TB_MAX_CLIENTS) return;

    tb_client* client = &ctx->clients[index];
    if(client->sock == TB_INVALID_SOCKET) return;
    if(client->outpos >= client->outlen) return;

    for(;;)
    {
        size_t remain = client->outlen - client->outpos;
        if(remain == 0) break;

        int r = (int)send(client->sock, client->outbuf + client->outpos, (int)remain, 0);
        if(r < 0)
        {
            int err = tb_last_socket_error();
            if(tb_would_block(err)) break;
            tb_disconnect_client(ctx, index);
            break;
        }

        client->outpos += (size_t)r;

        if(client->outpos == client->outlen)
        {
            client->outpos = 0;
            client->outlen = 0;
            free(client->outbuf);
            client->outbuf = NULL;
            break;
        }
    }
}

static void tb_handle_line(TicbuildRemoting* ctx, tb_client* client, const char* line, size_t n)
{
    char err[1000] = {0};

    // validation
    if(n > TB_LINE_LIMIT)
    {
        tb_send_response_str(client, 0, false, "line too long");
        return;
    }

    if(!tb_is_ascii_only(line, n))
    {
        tb_send_response_str(client, 0, false, "non-ascii input");
        return;
    }

    tb_trim_whitespace(&line, &n);
    if(n == 0) return; // only sent whitespace; just ignore it.

    const char* p = line;
    size_t len = n;

    tb_slice tok;
    if(!tb_next_token(&p, &len, &tok, err, sizeof err))
    {
        tb_send_response_str(client, 0, false, err[0] ? err : "missing id");
        return;
    }

    int64_t id;
    if(!tb_parse_int(tok, &id))
    {
        tb_send_response_str(client, 0, false, "invalid id");
        return;
    }

    if(!tb_next_token(&p, &len, &tok, err, sizeof err))
    {
        tb_send_response_str(client, id, false, "missing command");
        return;
    }

    // lowercase command
    char cmd[64];
    if(tok.len >= sizeof cmd)
    {
        tb_send_response_str(client, id, false, "command too long");
        return;
    }

    for(size_t i = 0; i < tok.len; i++)
        cmd[i] = (char)tolower((unsigned char)tok.ptr[i]);
    cmd[tok.len] = '\0';

    // Parse args
    tb_arg args[8];
    size_t argc = 0;

    while(1)
    {
        tb_slice atok;
        if(!tb_next_token(&p, &len, &atok, err, sizeof err))
            break;

        if(argc >= (sizeof args / sizeof args[0]))
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "too many args");
            return;
        }

        if(atok.len && atok.ptr[0] == '"')
        {
            char* decoded;
            size_t dlen;
            if(!tb_parse_quoted(atok.ptr, atok.len, &decoded, &dlen, err, sizeof err))
            {
                tb_free_args(args, argc);
                tb_send_response_str(client, id, false, err);
                return;
            }
            args[argc++] = (tb_arg){TB_ARG_STR, {.s = {decoded, dlen}}};
        }
        else if(atok.len && atok.ptr[0] == '<')
        {
            uint8_t* b;
            size_t blen;
            if(!tb_parse_bytes(atok.ptr, atok.len, &b, &blen, ctx, err, sizeof err))
            {
                tb_free_args(args, argc);
                tb_send_response_str(client, id, false, err);
                return;
            }
            args[argc++] = (tb_arg){TB_ARG_BYTES, {.b = {b, blen}}};
        }
        else
        {
            int64_t v;
            if(!tb_parse_int(atok, &v))
            {
                tb_free_args(args, argc);
                tb_send_response_str(client, id, false, "invalid number");
                return;
            }
            args[argc++] = (tb_arg){TB_ARG_INT, {.i = v}};
        }
    }

    // Dispatch
    if(strcmp(cmd, "ping") == 0)
    {
        tb_free_args(args, argc);
        tb_send_response_str(client, id, true, "PONG");
        return;
    }

    if(strcmp(cmd, "hello") == 0)
    {
        char buf[256] = {0};
        if(ctx->cb.hello) {
            ctx->cb.hello(ctx->cb.userdata, buf, sizeof buf);
        }
        else {
            snprintf(buf, sizeof buf, "TIC-80 remoting %s", TB_REMOTING_PROTOCOL_VERSION_STRING);
        }
        char esc[300];
        tb_escape_string(buf, strlen(buf), esc, sizeof esc);
        char data[340];
        snprintf(data, sizeof data, "\"%s\"", esc);

        tb_free_args(args, argc);
        tb_send_response_str(client, id, true, data);
        return;
    }

    if(strcmp(cmd, "sync") == 0)
    {
        if(argc != 1 || args[0].type != TB_ARG_INT)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> sync <flags>");
            return;
        }

        if(!ctx->cb.sync)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "sync not supported");
            return;
        }

        bool ok = ctx->cb.sync(ctx->cb.userdata, (uint32_t)args[0].v.i, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? NULL : err);
        return;
    }

    if(strcmp(cmd, "poke") == 0)
    {
        if(argc != 2 || args[0].type != TB_ARG_INT || args[1].type != TB_ARG_BYTES)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> poke <addr> <data>"
            );
            return;
        }

        if(!ctx->cb.poke)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "poke not supported");
            return;
        }

        bool ok = ctx->cb.poke(ctx->cb.userdata, (uint32_t)args[0].v.i, args[1].v.b.ptr, args[1].v.b.len, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? NULL : err);
        return;
    }

    if(strcmp(cmd, "peek") == 0)
    {
        if(argc != 2 || args[0].type != TB_ARG_INT || args[1].type != TB_ARG_INT)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> peek <addr> <size>");
            return;
        }

        if(!ctx->cb.peek)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "peek not supported");
            return;
        }

        uint32_t size = (uint32_t)args[1].v.i;
        if(size == 0 || size > TB_PEEK_LIMIT)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "invalid size");
            return;
        }

        uint8_t* tmp = (uint8_t*)malloc(size);
        if(!tmp)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "out of memory");
            return;
        }

        bool ok = ctx->cb.peek(ctx->cb.userdata, (uint32_t)args[0].v.i, size, tmp, err, sizeof err);
        tb_free_args(args, argc);

        if(ok)
            tb_send_response_bytes(client, id, tmp, size);
        else
            tb_send_response_str(client, id, false, err);

        free(tmp);
        return;
    }

    if(strcmp(cmd, "load") == 0)
    {
        if(argc != 2 || args[0].type != TB_ARG_STR || args[1].type != TB_ARG_INT)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> load \"path\" <run:1|0>");
            return;
        }

        if(!ctx->cb.load)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "load not supported");
            return;
        }

        bool run = args[1].v.i ? true : false;
        bool ok = ctx->cb.load(ctx->cb.userdata, args[0].v.s.ptr, run, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? NULL : err);
        return;
    }

    if(strcmp(cmd, "restart") == 0)
    {
        if(argc != 0)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> restart");
            return;
        }

        if(!ctx->cb.restart)
        {
            tb_send_response_str(client, id, false, "restart not supported");
            return;
        }

        bool ok = ctx->cb.restart(ctx->cb.userdata, err, sizeof err);
        tb_send_response_str(client, id, ok, ok ? NULL : err);
        return;
    }

    if(strcmp(cmd, "quit") == 0)
    {
        if(argc != 0)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> quit");
            return;
        }

        if(!ctx->cb.quit)
        {
            tb_send_response_str(client, id, false, "quit not supported");
            return;
        }

        bool ok = ctx->cb.quit(ctx->cb.userdata, err, sizeof err);
        tb_send_response_str(client, id, ok, ok ? NULL : err);
        return;
    }

    if(strcmp(cmd, "eval") == 0)
    {
        if(argc != 1 || args[0].type != TB_ARG_STR)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> eval \"code\"");
            return;
        }

        if(!ctx->cb.eval)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "eval not supported");
            return;
        }

        bool ok = ctx->cb.eval(ctx->cb.userdata, args[0].v.s.ptr, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? NULL : err);
        return;
    }

    if(strcmp(cmd, "evalexpr") == 0)
    {
        if(argc != 1 || args[0].type != TB_ARG_STR)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> evalexpr \"expr\"");
            return;
        }

        if(!ctx->cb.eval_expr)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "evalexpr not supported");
            return;
        }

        tb_text_buffer out;
        tb_text_response_init(&out);
        bool ok = ctx->cb.eval_expr(ctx->cb.userdata, args[0].v.s.ptr, &out, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? tb_text_buffer_data(&out) : err);
        tb_text_buffer_dispose(&out);
        return;
    }

    if(strcmp(cmd, "listglobals") == 0)
    {
        if(argc != 0)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> listglobals");
            return;
        }

        if(!ctx->cb.list_globals)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "listglobals not supported");
            return;
        }

        tb_text_buffer out;
        tb_text_response_init(&out);
        bool ok = ctx->cb.list_globals(ctx->cb.userdata, &out, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? tb_text_buffer_data(&out) : err);
        tb_text_buffer_dispose(&out);
        return;
    }

    if(strcmp(cmd, "getfps") == 0)
    {
        if(argc != 0)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> getfps");
            return;
        }

        char data[32];
        snprintf(data, sizeof data, "%d", ticbuild_remoting_get_fps(ctx));
        tb_free_args(args, argc);
        tb_send_response_str(client, id, true, data);
        return;
    }

    if(strcmp(cmd, "perf") == 0)
    {
        if(argc != 0)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> perf");
            return;
        }

        char fpsbuf[32];
        char data[512];
        tb_format_fps(fpsbuf, sizeof fpsbuf, &ctx->fps);

        snprintf(data, sizeof data,
            "client_count=%d,"
            "fps=%s,"
            "tic_cycles=%llu,"
            "scn_cycles=%llu,"
            "bdr_cycles=%llu,"
            "lua_gc_mem=%llu",
            ctx->client_count,
            fpsbuf,
            (unsigned long long)ctx->user_tic_cycles,
            (unsigned long long)ctx->user_scn_cycles,
            (unsigned long long)ctx->user_bdr_cycles,
            (unsigned long long)ctx->lua_gc_mem_bytes);

        tb_free_args(args, argc);
        tb_send_response_str(client, id, true, data);
        return;
    }

    if(strcmp(cmd, "cartpath") == 0)
    {
        if(argc != 0)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> cartpath");
            return;
        }

        if(!ctx->cb.cart_path)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "cartpath not supported");
            return;
        }

        tb_text_buffer out;
        tb_text_response_init(&out);
        bool ok = ctx->cb.cart_path(ctx->cb.userdata, &out, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? tb_text_buffer_data(&out) : err);
        tb_text_buffer_dispose(&out);
        return;
    }

    if(strcmp(cmd, "fs") == 0)
    {
        if(argc != 0)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> fs");
            return;
        }

        if(!ctx->cb.fs_path)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "fs not supported");
            return;
        }

        tb_text_buffer out;
        tb_text_response_init(&out);
        bool ok = ctx->cb.fs_path(ctx->cb.userdata, &out, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? tb_text_buffer_data(&out) : err);
        tb_text_buffer_dispose(&out);
        return;
    }

    if(strcmp(cmd, "metadata") == 0)
    {
        if(argc != 1 || args[0].type != TB_ARG_STR)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "usage: <id> metadata \"key\"");
            return;
        }

        if(!ctx->cb.metadata)
        {
            tb_free_args(args, argc);
            tb_send_response_str(client, id, false, "metadata not supported");
            return;
        }

        tb_text_buffer out;
        tb_text_response_init(&out);
        bool ok = ctx->cb.metadata(ctx->cb.userdata, args[0].v.s.ptr, &out, err, sizeof err);
        tb_free_args(args, argc);
        tb_send_response_str(client, id, ok, ok ? tb_text_buffer_data(&out) : err);
        tb_text_buffer_dispose(&out);
        return;
    }

    tb_free_args(args, argc);
    tb_send_response_str(client, id, false, "unknown command");
}

// pushes complete lines out of inbuf to handler.
static void tb_process_input(TicbuildRemoting* ctx, int index)
{
    if(index < 0 || index >= TB_MAX_CLIENTS) return;

    tb_client* client = &ctx->clients[index];
    if(client->inlen == 0) return;

    // process in complete lines
    size_t start = 0;
    for(size_t i = 0; i < client->inlen; i++)
    {
        if(client->inbuf[i] == '\n')
        {
            size_t linelen = i - start;
            // for CRLF line endings, trim the CR
            if(linelen && client->inbuf[start + linelen - 1] == '\r')
                linelen--;

            // dispatch in complete lines
            tb_handle_line(ctx, client, client->inbuf + start, linelen);
            start = i + 1;
        }
    }

    // move remaining partial line to start of buffer
    if(start > 0)
    {
        memmove(client->inbuf, client->inbuf + start, client->inlen - start);
        client->inlen -= start;
    }
}

TicbuildRemoting* ticbuild_remoting_create(int port, const char* session_dir, bool global_disco, const ticbuild_remoting_callbacks* callbacks)
{
    if(port <= 0) return NULL;

    TicbuildRemoting* ctx = (TicbuildRemoting*)calloc(1, sizeof(TicbuildRemoting));
    if(!ctx) return NULL;

    ctx->port = port;
    if(callbacks) ctx->cb = *callbacks;

    if(session_dir && session_dir[0])
        ctx->discovery_dir = strdup(session_dir);
    ctx->global_disco = global_disco;

    tb_fps_init(&ctx->fps);

    ctx->listen_sock = TB_INVALID_SOCKET;
    ctx->client_count = 0;
    ctx->last_client_count = 0;
    for(int i = 0; i < TB_MAX_CLIENTS; i++)
    {
        ctx->clients[i].sock = TB_INVALID_SOCKET;
        ctx->clients[i].inbuf = NULL;
        ctx->clients[i].inlen = 0;
        ctx->clients[i].outbuf = NULL;
        ctx->clients[i].outlen = 0;
        ctx->clients[i].outpos = 0;
    }

    char err[128];
    if(!tb_socket_init(ctx, err, sizeof err))
    {
        ticbuild_remoting_close(ctx);
        return NULL;
    }

    return ctx;
}

void ticbuild_remoting_close(TicbuildRemoting* ctx)
{
    if(!ctx) return;

    for(int i = 0; i < TB_MAX_CLIENTS; i++)
        tb_disconnect_client(ctx, i);

    tb_discovery_stop();

    if(ctx->listen_sock != TB_INVALID_SOCKET)
    {
        tb_close_socket(ctx->listen_sock);
        ctx->listen_sock = TB_INVALID_SOCKET;
    }

    if(ctx->wsa_started)
        WSACleanup();

    free(ctx->tmpbytes);
    free(ctx->discovery_dir);
    free(ctx);
}

void ticbuild_remoting_tick(TicbuildRemoting* ctx)
{
    if(!ctx) return;

    char err[128];
    if(!tb_socket_init(ctx, err, sizeof err))
    {
        tb_discovery_stop();
        tb_set_err(ctx->last_listen_err, sizeof ctx->last_listen_err, err[0] ? err : "socket init failed");
        tb_mark_title_dirty(ctx);
        return;
    }

    // Clear previous error once we're healthy.
    if(ctx->last_listen_err[0])
    {
        ctx->last_listen_err[0] = '\0';
        tb_mark_title_dirty(ctx);
    }

    tb_accept_client(ctx);

    for(int i = 0; i < TB_MAX_CLIENTS; i++)
    {
        if(ctx->clients[i].sock == TB_INVALID_SOCKET) continue;
        tb_read_client(ctx, i);
        tb_process_input(ctx, i);
        tb_flush_output(ctx, i);
    }

    // Track connect/disconnect changes that happened during the tick.
    if(ctx->client_count != ctx->last_client_count)
    {
        ctx->last_client_count = ctx->client_count;
        tb_mark_title_dirty(ctx);
    }
}

#endif // __EMSCRIPTEN__
