#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <lua.h>

typedef struct tb_text_buffer tb_text_buffer;

bool tb_lua_serialize_expr(lua_State* lua, int index, tb_text_buffer* out, char* err, size_t errcap);
