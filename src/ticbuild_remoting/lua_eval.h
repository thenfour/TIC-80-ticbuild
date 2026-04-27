#pragma once

#include "api.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct tb_text_buffer tb_text_buffer;

bool tb_lua_eval_expr(tic_mem* tic, const char* expr, tb_text_buffer* out, char* err, size_t errcap);
bool tb_lua_list_globals(tic_mem* tic, tb_text_buffer* out, char* err, size_t errcap);
