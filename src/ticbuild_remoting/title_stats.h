#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ticbuild_remoting/fps.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        tb_fps_tracker fps;
        bool dirty;
        uint32_t tic_ms10;
        uint32_t scn_ms10;
        uint32_t bdr_ms10;
        uint32_t total_ms10;
        uint64_t total_ticks;
        uint64_t timing_freq;
        uint64_t tic_cycles;
        uint64_t scn_cycles;
        uint64_t bdr_cycles;
        uint64_t lua_mem_bytes;
    } tb_title_stats;

    void tb_title_stats_on_frame(tb_title_stats* stats, uint64_t counter, uint64_t freq);
    int tb_title_stats_get_fps(const tb_title_stats* stats);
    int tb_title_stats_get_uncapped_fps(const tb_title_stats* stats);
    void tb_title_stats_set_user_time_ms10(tb_title_stats* stats, uint32_t tic_ms10, uint32_t scn_ms10, uint32_t bdr_ms10, uint32_t total_ms10);
    void tb_title_stats_set_user_time_ticks(tb_title_stats* stats, uint64_t total_ticks, uint64_t timing_freq);
    void tb_title_stats_set_lua_perf(tb_title_stats* stats, uint64_t tic_cycles, uint64_t scn_cycles, uint64_t bdr_cycles, uint64_t lua_mem_bytes);
    void tb_title_stats_get_title_info(const tb_title_stats* stats, char* out, size_t outcap);
    bool tb_title_stats_take_dirty(tb_title_stats* stats);

#ifdef __cplusplus
}
#endif