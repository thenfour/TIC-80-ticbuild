#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        TB_LUA_PERF_USER_SLOT_COUNT = 4,
        TB_LUA_PERF_LABEL_MAX = 31,
    };

    typedef enum
    {
        TB_LUA_PROFILER_MODE_OFF = 0,
        TB_LUA_PROFILER_MODE_INSTRUCTIONS,
        TB_LUA_PROFILER_MODE_WALLCLOCK,
    } tb_lua_profiler_mode;

    typedef struct
    {
        bool running;
        tb_lua_profiler_mode mode;
        uint32_t instruction_interval;
        uint32_t wall_clock_period_micros;
        uint32_t elapsed_seconds;
    } tb_lua_profiler_status;

    typedef struct
    {
        bool active;
        double value;
        double smoothing_alpha;
        char label[TB_LUA_PERF_LABEL_MAX + 1];
    } tb_lua_perf_user_slot;

    // Ensure the Lua instruction-count hook is installed for the current VM.
    void ticbuild_lua_perf_install(tic_mem* tic);

    // Reset the per-frame instruction counter.
    void ticbuild_lua_perf_reset_counter(tic_mem* tic);

    // Returns the current instruction counter (0 if unavailable).
    uint64_t ticbuild_lua_perf_get_counter(tic_mem* tic);

    // Returns Lua GC memory usage in bytes (0 if unavailable).
    uint64_t ticbuild_lua_perf_get_mem_bytes(tic_mem* tic);

    // Sets/updates a user perf slot.
    // `set_label` / `set_alpha` control whether the corresponding values are updated.
    bool ticbuild_lua_perf_set_user_slot(
        tic_mem* tic,
        int slot,
        double value,
        const char* label,
        bool set_label,
        double smoothing_alpha,
        bool set_alpha);

    // Clears/hides a user perf slot.
    bool ticbuild_lua_perf_clear_user_slot(tic_mem* tic, int slot);

    // Copies current user perf slots into `out_slots`.
    // If unavailable, writes defaults (inactive, zero value).
    void ticbuild_lua_perf_get_user_slots(tic_mem* tic, tb_lua_perf_user_slot out_slots[TB_LUA_PERF_USER_SLOT_COUNT]);

    bool ticbuild_lua_profiler_start(
        tic_mem* tic,
        const char* mode,
        uint32_t instruction_interval,
        uint32_t wall_clock_period_micros,
        char* err,
        size_t errcap);

    bool ticbuild_lua_profiler_stop(
        tic_mem* tic,
        const char* output_path,
        char* saved_path,
        size_t saved_path_cap,
        char* err,
        size_t errcap);

    bool ticbuild_lua_profiler_get_status(tic_mem* tic, tb_lua_profiler_status* out_status);

#ifdef __cplusplus
}
#endif
