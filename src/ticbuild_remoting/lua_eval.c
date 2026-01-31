#include "lua_eval.h"

#include "core/core.h"
#include "lua_serialize.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
