#include "lua_serialize.h"

#include "api/lua_value_serialize.h"
#include "api/luaapi.h"
#include "core/core.h"
#include "ticbuild_remoting/utils.h"

#include <lua.h>

typedef struct
{
    tb_text_buffer* out;
    char* err;
    size_t errcap;
} tb_lua_serialize_output;

static bool appendSerializedValue(void* data, const char* text, size_t length)
{
    tb_lua_serialize_output* output = (tb_lua_serialize_output*)data;
    return tb_text_buffer_append(output->out, text, length, output->err, output->errcap);
}

bool tb_lua_serialize_expr(lua_State* lua, int index, tb_text_buffer* out, char* err, size_t errcap)
{
    if(!out)
    {
        tb_set_err(err, errcap, "missing output buffer");
        return false;
    }

    tb_lua_serialize_output output =
    {
        .out = out,
        .err = err,
        .errcap = errcap,
    };

    return luaapi_serialize_value(lua, index, appendSerializedValue, &output, err, errcap);
}

bool tb_lua_capture_hmr_state(tic_mem* tic, tb_text_buffer* out)
{
    tic_core* core = (tic_core*)tic;
    lua_State* lua = core ? (lua_State*)core->currentVM : NULL;

    if(!lua || !out || !luaapi_hmr_push_saved_value(tic))
        return false;

    char err[128];
    bool ok = tb_lua_serialize_expr(lua, -1, out, err, sizeof err);
    lua_settop(lua, 0);
    return ok;
}
