#include "lua_serialize.h"

#include "api/luaapi.h"
#include "core/core.h"
#include "ticbuild_remoting/utils.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>

#include <lauxlib.h>

#define TB_LUA_SERIALIZE_MAX_DEPTH 32

typedef struct
{
    tb_text_buffer* out;
    char* err;
    size_t errcap;
} tb_lua_ser_ctx;

static void tb_lua_ser_set_err(tb_lua_ser_ctx* ctx, const char* msg)
{
    if(ctx->err && ctx->errcap)
    {
        strncpy(ctx->err, msg ? msg : "error", ctx->errcap - 1);
        ctx->err[ctx->errcap - 1] = '\0';
    }
}

static bool tb_append(tb_lua_ser_ctx* ctx, const char* s, size_t n)
{
    return tb_text_buffer_append(ctx->out, s, n, ctx->err, ctx->errcap);
}

static bool tb_append_char(tb_lua_ser_ctx* ctx, char c)
{
    return tb_append(ctx, &c, 1);
}

static bool tb_append_cstr(tb_lua_ser_ctx* ctx, const char* s)
{
    return tb_append(ctx, s, strlen(s));
}

static void tb_lua_ser_set_err_fmt(tb_lua_ser_ctx* ctx, const char* fmt, const char* arg)
{
    if(ctx->err && ctx->errcap)
    {
        snprintf(ctx->err, ctx->errcap, fmt, arg ? arg : "");
        ctx->err[ctx->errcap - 1] = '\0';
    }
}

static bool tb_is_identifier(const char* s, size_t len)
{
    if(len == 0) return false;

    unsigned char c = (unsigned char)s[0];
    if(!(isalpha(c) || c == '_')) return false;

    for(size_t i = 1; i < len; i++)
    {
        c = (unsigned char)s[i];
        if(!(isalnum(c) || c == '_')) return false;
    }

    return true;
}

static bool tb_serialize_value(lua_State* lua, int index, tb_lua_ser_ctx* ctx, int depth, int visited_index);

static bool tb_serialize_string(const char* s, size_t len, tb_lua_ser_ctx* ctx)
{
    if(!tb_append_char(ctx, '"')) return false;

    size_t esc_len = tb_escape_string_len(s ? s : "", len);
    char stack_buf[256];
    char* esc = stack_buf;
    if(esc_len + 1 > sizeof stack_buf)
    {
        esc = (char*)malloc(esc_len + 1);
        if(!esc)
        {
            tb_lua_ser_set_err(ctx, "out of memory");
            return false;
        }
    }

    size_t wrote = tb_escape_string(s ? s : "", len, esc, esc_len + 1);
    bool ok = tb_append(ctx, esc, wrote);
    if(esc != stack_buf) free(esc);
    if(!ok) return false;

    return tb_append_char(ctx, '"');
}

static bool tb_serialize_number(lua_State* lua, int index, tb_lua_ser_ctx* ctx)
{
#if LUA_VERSION_NUM >= 503
    if(lua_isinteger(lua, index))
    {
        char buf[64];
        snprintf(buf, sizeof buf, "%lld", (long long)lua_tointeger(lua, index));
        return tb_append_cstr(ctx, buf);
    }
#endif

    lua_Number n = lua_tonumber(lua, index);
    double nd = (double)n;
#if defined(_MSC_VER)
    if(!_finite(nd))
#else
    if(!isfinite(nd))
#endif
    {
        tb_lua_ser_set_err(ctx, "unsupported number value");
        return false;
    }

    char buf[512];
    int len = snprintf(buf, sizeof buf, "%.17f", nd);
    if(len < 0 || (size_t)len >= sizeof buf)
    {
        tb_lua_ser_set_err(ctx, "number too large");
        return false;
    }

    tb_trim_trailing_zeros(buf);
    return tb_append_cstr(ctx, buf);
}

static bool tb_serialize_table(lua_State* lua, int index, tb_lua_ser_ctx* ctx, int depth, int visited_index)
{
    if(depth > TB_LUA_SERIALIZE_MAX_DEPTH)
    {
        tb_lua_ser_set_err(ctx, "table too deep");
        return false;
    }

    index = lua_absindex(lua, index);
    visited_index = lua_absindex(lua, visited_index);

    lua_pushvalue(lua, index);
    lua_rawget(lua, visited_index);
    if(!lua_isnil(lua, -1))
    {
        lua_pop(lua, 1);
        tb_lua_ser_set_err(ctx, "cycle detected");
        return false;
    }
    lua_pop(lua, 1);

    lua_pushvalue(lua, index);
    lua_pushboolean(lua, 1);
    lua_rawset(lua, visited_index);

    if(!tb_append_char(ctx, '{')) return false;

    size_t array_len = (size_t)lua_rawlen(lua, index);
    bool has_items = false;

    for(size_t i = 1; i <= array_len; i++)
    {
        if(has_items)
        {
            if(!tb_append_char(ctx, ',')) return false;
        }

        lua_rawgeti(lua, index, (lua_Integer)i);
        if(!tb_serialize_value(lua, -1, ctx, depth + 1, visited_index))
        {
            lua_pop(lua, 1);
            return false;
        }
        lua_pop(lua, 1);
        has_items = true;
    }

    lua_pushnil(lua);
    while(lua_next(lua, index) != 0)
    {
        bool skip = false;

        if(lua_isinteger(lua, -2))
        {
            lua_Integer k = lua_tointeger(lua, -2);
            if(k >= 1 && (size_t)k <= array_len)
                skip = true;
        }

        if(!skip)
        {
            int ktype = lua_type(lua, -2);
            if(ktype == LUA_TTABLE || ktype == LUA_TFUNCTION || ktype == LUA_TTHREAD || ktype == LUA_TUSERDATA || ktype == LUA_TLIGHTUSERDATA)
            {
                lua_pop(lua, 2);
                tb_lua_ser_set_err(ctx, "unsupported key type");
                return false;
            }

            if(has_items)
            {
                if(!tb_append_char(ctx, ',')) { lua_pop(lua, 2); return false; }
            }

            if(ktype == LUA_TSTRING)
            {
                size_t klen = 0;
                const char* kstr = lua_tolstring(lua, -2, &klen);
                if(kstr && tb_is_identifier(kstr, klen))
                {
                    if(!tb_append(ctx, kstr, klen)) { lua_pop(lua, 2); return false; }
                    if(!tb_append_char(ctx, '=')) { lua_pop(lua, 2); return false; }
                }
                else
                {
                    if(!tb_append_char(ctx, '[')) { lua_pop(lua, 2); return false; }
                    if(!tb_serialize_value(lua, -2, ctx, depth + 1, visited_index)) { lua_pop(lua, 2); return false; }
                    if(!tb_append_cstr(ctx, "]=")) { lua_pop(lua, 2); return false; }
                }
            }
            else
            {
                if(!tb_append_char(ctx, '[')) { lua_pop(lua, 2); return false; }
                if(!tb_serialize_value(lua, -2, ctx, depth + 1, visited_index)) { lua_pop(lua, 2); return false; }
                if(!tb_append_cstr(ctx, "]=")) { lua_pop(lua, 2); return false; }
            }

            if(!tb_serialize_value(lua, -1, ctx, depth + 1, visited_index))
            {
                lua_pop(lua, 2);
                return false;
            }

            has_items = true;
        }

        lua_pop(lua, 1);
    }

    return tb_append_char(ctx, '}');
}

static bool tb_serialize_value(lua_State* lua, int index, tb_lua_ser_ctx* ctx, int depth, int visited_index)
{
    index = lua_absindex(lua, index);

    int type = lua_type(lua, index);
    switch(type)
    {
        case LUA_TNIL:
            return tb_append_cstr(ctx, "nil");
        case LUA_TBOOLEAN:
            return tb_append_cstr(ctx, lua_toboolean(lua, index) ? "true" : "false");
        case LUA_TNUMBER:
            return tb_serialize_number(lua, index, ctx);
        case LUA_TSTRING:
        {
            size_t slen = 0;
            const char* s = lua_tolstring(lua, index, &slen);
            return tb_serialize_string(s ? s : "", slen, ctx);
        }
        case LUA_TTABLE:
            return tb_serialize_table(lua, index, ctx, depth, visited_index);
        default:
        {
            const char* tname = lua_typename(lua, type);
            if(tname)
                tb_lua_ser_set_err_fmt(ctx, "unsupported result type: %s", tname);
            else
                tb_lua_ser_set_err(ctx, "unsupported result type");
            return false;
        }
    }
}

bool tb_lua_serialize_expr(lua_State* lua, int index, tb_text_buffer* out, char* err, size_t errcap)
{
    tb_lua_ser_ctx ctx =
    {
        .out = out,
        .err = err,
        .errcap = errcap,
    };

    if(err && errcap) err[0] = '\0';

    if(!out)
    {
        if(err && errcap)
        {
            strncpy(err, "missing output buffer", errcap - 1);
            err[errcap - 1] = '\0';
        }
        return false;
    }

    int abs_index = lua_absindex(lua, index);

    lua_newtable(lua);
    int visited_index = lua_gettop(lua);

    bool ok = tb_serialize_value(lua, abs_index, &ctx, 0, visited_index);

    lua_pop(lua, 1);

    return ok;
}

bool tb_lua_capture_hmr_state(tic_mem* tic, tb_text_buffer* out)
{
    tic_core* core = (tic_core*)tic;
    lua_State* lua = core ? core->currentVM : NULL;

    if(!lua || !out || !luaapi_hmr_push_saved_value(tic))
        return false;

    char err[128];
    bool ok = tb_lua_serialize_expr(lua, -1, out, err, sizeof err);
    lua_settop(lua, 0);
    return ok;
}
