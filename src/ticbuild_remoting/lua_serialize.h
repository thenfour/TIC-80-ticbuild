#pragma once

#include <stdbool.h>
#include <stddef.h>


#include <lua.h>

bool tb_lua_serialize_expr(lua_State* lua, int index, char* out, size_t outcap, char* err, size_t errcap);
