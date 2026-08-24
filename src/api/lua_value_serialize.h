#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <lua.h>

typedef bool (*luaapi_value_write)(void* data, const char* text, size_t length);

// Serializes the value using the same Lua-expression representation used by
// remoting eval_expr. The writer controls the output budget; returning false
// stops serialization without invoking Lua metamethods.
bool luaapi_serialize_value(
    lua_State* lua,
    int index,
    luaapi_value_write write,
    void* writeData,
    char* err,
    size_t errcap);
