#pragma once

#include "api.h"

#include <stdbool.h>
#include <stddef.h>

bool tb_lua_eval_expr(tic_mem* tic, const char* expr, char* out, size_t outcap, char* err, size_t errcap);
