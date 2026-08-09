// MIT License

// Copyright (c) 2017 Vadim Grigoruk @nesbox // grigoruk@gmail.com

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "core/core.h"

#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <ctype.h>

void luaapi_init(tic_core* core);
void luaapi_tick(tic_mem* tic);
void luaapi_boot(tic_mem* tic);
void luaapi_scn(tic_mem* tic, s32 row, void* data);
void luaapi_bdr(tic_mem* tic, s32 row, void* data);
void luaapi_menu(tic_mem* tic, s32 index, void* data);
void luaapi_close(tic_mem* tic);
void luaapi_open(lua_State *lua);

typedef enum
{
    LUAAPI_HMR_NONE,
    LUAAPI_HMR_SAVER_INSTALLED,
    LUAAPI_HMR_ERROR,
} luaapi_hmr_result;

// Invokes the saver installed by the most recent HMR() call. On success, the
// single non-nil value to serialize is left at the top of the Lua stack.
bool luaapi_hmr_push_saved_value(tic_mem* tic);

// Invokes global HMR(saved) after BOOT. The returned function, if any, becomes
// the saver for this VM generation.
luaapi_hmr_result luaapi_hmr_restore(tic_mem* tic, const char* saved, size_t savedSize, bool hasSaved);
