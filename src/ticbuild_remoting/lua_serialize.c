#include "lua_serialize.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <lauxlib.h>

#define TB_LUA_SERIALIZE_MAX_DEPTH 32

typedef struct
{
    char* out;
    size_t cap;
    size_t len;
    char* err;
    size_t errcap;
} tb_lua_ser_ctx;

static void tb_set_err(tb_lua_ser_ctx* ctx, const char* msg)
{
    if(ctx->err && ctx->errcap)
    {
        strncpy(ctx->err, msg ? msg : "error", ctx->errcap - 1);
        ctx->err[ctx->errcap - 1] = '\0';
    }
}

static bool tb_append(tb_lua_ser_ctx* ctx, const char* s, size_t n)
{
    if(!ctx->out || ctx->cap == 0)
        return false;

    if(ctx->len + n + 1 > ctx->cap)
    {
        tb_set_err(ctx, "result too large");
        return false;
    }

    memcpy(ctx->out + ctx->len, s, n);
    ctx->len += n;
    ctx->out[ctx->len] = '\0';
    return true;
}

static bool tb_append_char(tb_lua_ser_ctx* ctx, char c)
{
    return tb_append(ctx, &c, 1);
}

static bool tb_append_cstr(tb_lua_ser_ctx* ctx, const char* s)
{
    return tb_append(ctx, s, strlen(s));
}

static void tb_set_err_fmt(tb_lua_ser_ctx* ctx, const char* fmt, const char* arg)
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

    for(size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)s[i];
        switch(c)
        {
            case '\\': if(!tb_append_cstr(ctx, "\\\\")) return false; break;
            case '"':  if(!tb_append_cstr(ctx, "\\\"")) return false; break;
            case '\n': if(!tb_append_cstr(ctx, "\\n")) return false; break;
            case '\r': if(!tb_append_cstr(ctx, "\\r")) return false; break;
            case '\t': if(!tb_append_cstr(ctx, "\\t")) return false; break;
            case '\b': if(!tb_append_cstr(ctx, "\\b")) return false; break;
            case '\f': if(!tb_append_cstr(ctx, "\\f")) return false; break;
            default:
                if(c < 32 || c == 127)
                {
                    char esc[5];
                    snprintf(esc, sizeof esc, "\\x%02X", (unsigned)c);
                    if(!tb_append(ctx, esc, 4)) return false;
                }
                else
                {
                    if(!tb_append_char(ctx, (char)c)) return false;
                }
                break;
        }
    }

    return tb_append_char(ctx, '"');
}

static bool tb_serialize_table(lua_State* lua, int index, tb_lua_ser_ctx* ctx, int depth, int visited_index)
{
    if(depth > TB_LUA_SERIALIZE_MAX_DEPTH)
    {
        tb_set_err(ctx, "table too deep");
        return false;
    }

    index = lua_absindex(lua, index);
    visited_index = lua_absindex(lua, visited_index);

    lua_pushvalue(lua, index);
    lua_rawget(lua, visited_index);
    if(!lua_isnil(lua, -1))
    {
        lua_pop(lua, 1);
        tb_set_err(ctx, "cycle detected");
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
                tb_set_err(ctx, "unsupported key type");
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
        {
            size_t nlen = 0;
            const char* nstr = luaL_tolstring(lua, index, &nlen);
            bool ok = nstr && tb_append(ctx, nstr, nlen);
            lua_pop(lua, 1);
            return ok;
        }
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
                tb_set_err_fmt(ctx, "unsupported result type: %s", tname);
            else
                tb_set_err(ctx, "unsupported result type");
            return false;
        }
    }
}

bool tb_lua_serialize_expr(lua_State* lua, int index, char* out, size_t outcap, char* err, size_t errcap)
{
    tb_lua_ser_ctx ctx =
    {
        .out = out,
        .cap = outcap,
        .len = 0,
        .err = err,
        .errcap = errcap,
    };

    if(out && outcap) out[0] = '\0';
    if(err && errcap) err[0] = '\0';

    int abs_index = lua_absindex(lua, index);

    lua_newtable(lua);
    int visited_index = lua_gettop(lua);

    bool ok = tb_serialize_value(lua, abs_index, &ctx, 0, visited_index);

    lua_pop(lua, 1);

    return ok;
}
