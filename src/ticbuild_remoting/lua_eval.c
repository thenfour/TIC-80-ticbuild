#include "lua_eval.h"

#include "core/core.h"
#include "lua_serialize.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

bool tb_lua_eval_expr(tic_mem* tic, const char* expr, char* out, size_t outcap, char* err, size_t errcap)
{
    if(out && outcap) out[0] = '\0';
    if(err && errcap) err[0] = '\0';

    if(!expr || !expr[0])
    {
        if(err && errcap) { strncpy(err, "missing expression", errcap - 1); err[errcap - 1] = '\0'; }
        return false;
    }

    tic_core* core = (tic_core*)tic;
    lua_State* lua = core->currentVM;

    if(!lua)
    {
        if(err && errcap) { strncpy(err, "lua not available", errcap - 1); err[errcap - 1] = '\0'; }
        return false;
    }

    const char prefix[] = "return ";
    size_t expr_len = strlen(expr);
    size_t prefix_len = sizeof prefix - 1;
    char* chunk = (char*)malloc(prefix_len + expr_len + 1);
    if(!chunk)
    {
        if(err && errcap) { strncpy(err, "out of memory", errcap - 1); err[errcap - 1] = '\0'; }
        return false;
    }

    memcpy(chunk, prefix, prefix_len);
    memcpy(chunk + prefix_len, expr, expr_len + 1);

    lua_settop(lua, 0);

    if(luaL_loadstring(lua, chunk) != LUA_OK || lua_pcall(lua, 0, 1, 0) != LUA_OK)
    {
        const char* msg = lua_tostring(lua, -1);
        if(err && errcap)
        {
            strncpy(err, msg ? msg : "eval failed", errcap - 1);
            err[errcap - 1] = '\0';
        }
        lua_settop(lua, 0);
        free(chunk);
        return false;
    }

    free(chunk);

    bool ok = tb_lua_serialize_expr(lua, -1, out, outcap, err, errcap);
    lua_settop(lua, 0);
    return ok;
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

static bool tb_is_lua_keyword(const char* s, size_t len)
{
    static const char* const keywords[] =
    {
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
        "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
        "then", "true", "until", "while",
    };

    for(size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++)
    {
        const char* k = keywords[i];
        if(strlen(k) == len && strncmp(k, s, len) == 0)
            return true;
    }

    return false;
}

static int tb_strcmp_qsort(const void* a, const void* b)
{
    const char* const* sa = (const char* const*)a;
    const char* const* sb = (const char* const*)b;
    return strcmp(*sa, *sb);
}

bool tb_lua_list_globals(tic_mem* tic, char* out, size_t outcap, char* err, size_t errcap)
{
    if(out && outcap) out[0] = '\0';
    if(err && errcap) err[0] = '\0';

    tic_core* core = (tic_core*)tic;
    lua_State* lua = core ? core->currentVM : NULL;

    if(!lua)
    {
        if(err && errcap) { strncpy(err, "lua not available", errcap - 1); err[errcap - 1] = '\0'; }
        return false;
    }

    if(!out || outcap == 0)
    {
        if(err && errcap) { strncpy(err, "missing output buffer", errcap - 1); err[errcap - 1] = '\0'; }
        return false;
    }

    lua_settop(lua, 0);

#if LUA_VERSION_NUM >= 502
    lua_pushglobaltable(lua);
#else
    lua_getglobal(lua, "_G");
#endif

    if(!lua_istable(lua, -1))
    {
        if(err && errcap) { strncpy(err, "global table not available", errcap - 1); err[errcap - 1] = '\0'; }
        lua_settop(lua, 0);
        return false;
    }

    size_t count = 0;
    size_t cap = 64;
    char** names = (char**)malloc(sizeof(char*) * cap);
    if(!names)
    {
        if(err && errcap) { strncpy(err, "out of memory", errcap - 1); err[errcap - 1] = '\0'; }
        lua_settop(lua, 0);
        return false;
    }

    lua_pushnil(lua);
    while(lua_next(lua, -2) != 0)
    {
        if(lua_type(lua, -2) == LUA_TSTRING)
        {
            size_t klen = 0;
            const char* kstr = lua_tolstring(lua, -2, &klen);
            if(kstr && tb_is_identifier(kstr, klen) && !tb_is_lua_keyword(kstr, klen))
            {
                if(count == cap)
                {
                    size_t ncap = cap * 2;
                    char** nnames = (char**)realloc(names, sizeof(char*) * ncap);
                    if(!nnames)
                    {
                        if(err && errcap) { strncpy(err, "out of memory", errcap - 1); err[errcap - 1] = '\0'; }
                        lua_settop(lua, 0);
                        for(size_t i = 0; i < count; i++) free(names[i]);
                        free(names);
                        return false;
                    }
                    names = nnames;
                    cap = ncap;
                }

                char* copy = (char*)malloc(klen + 1);
                if(!copy)
                {
                    if(err && errcap) { strncpy(err, "out of memory", errcap - 1); err[errcap - 1] = '\0'; }
                    lua_settop(lua, 0);
                    for(size_t i = 0; i < count; i++) free(names[i]);
                    free(names);
                    return false;
                }

                memcpy(copy, kstr, klen);
                copy[klen] = '\0';
                names[count++] = copy;
            }
        }

        lua_pop(lua, 1);
    }

    lua_settop(lua, 0);

    if(count > 1)
        qsort(names, count, sizeof(char*), tb_strcmp_qsort);

    size_t outlen = 0;
    for(size_t i = 0; i < count; i++)
    {
        size_t nlen = strlen(names[i]);
        size_t extra = (i == 0) ? 0 : 1;
        if(outlen + nlen + extra + 1 >= outcap)
        {
            if(err && errcap) { strncpy(err, "result too large", errcap - 1); err[errcap - 1] = '\0'; }
            for(size_t j = 0; j < count; j++) free(names[j]);
            free(names);
            return false;
        }

        if(i != 0)
            out[outlen++] = ',';

        memcpy(out + outlen, names[i], nlen);
        outlen += nlen;
        out[outlen] = '\0';
    }

    if(outlen == 0)
        out[0] = '\0';

    for(size_t i = 0; i < count; i++) free(names[i]);
    free(names);

    return true;
}
