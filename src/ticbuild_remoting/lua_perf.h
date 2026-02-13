#pragma once

#include <stdint.h>

#include "api.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Ensure the Lua instruction-count hook is installed for the current VM.
    void ticbuild_lua_perf_install(tic_mem* tic);

    // Reset the per-frame instruction counter.
    void ticbuild_lua_perf_reset_counter(tic_mem* tic);

    // Returns the current instruction counter (0 if unavailable).
    uint64_t ticbuild_lua_perf_get_counter(tic_mem* tic);

    // Returns Lua GC memory usage in bytes (0 if unavailable).
    uint64_t ticbuild_lua_perf_get_mem_bytes(tic_mem* tic);

#ifdef __cplusplus
}
#endif
