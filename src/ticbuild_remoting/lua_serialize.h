#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <lua.h>

typedef struct tb_text_buffer tb_text_buffer;
typedef struct tic_mem tic_mem;

bool tb_lua_serialize_expr(lua_State* lua, int index, tb_text_buffer* out, char* err, size_t errcap);

// Best-effort: returns false when no saver/value is available or
// when callback execution/serialization fails; those failures are silent by
// contract and must not stop a cart reload.
bool tb_lua_capture_hmr_state(tic_mem* tic, tb_text_buffer* out);
