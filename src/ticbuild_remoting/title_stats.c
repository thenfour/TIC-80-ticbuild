#include "ticbuild_remoting/title_stats.h"

#include "ticbuild_remoting/utils.h"

#include <stdio.h>

static unsigned tb_round_kilo(uint64_t value)
{
    return (unsigned)((value + 500ULL) / 1000ULL);
}

static unsigned tb_round_kb(uint64_t bytes)
{
    return (unsigned)((bytes + 512ULL) / 1024ULL);
}

void tb_title_stats_on_frame(tb_title_stats* stats, uint64_t counter, uint64_t freq)
{
    if(!stats) return;

    if(tb_fps_on_frame(&stats->fps, counter, freq))
        stats->dirty = true;
}

int tb_title_stats_get_fps(const tb_title_stats* stats)
{
    return stats ? tb_fps_get(&stats->fps) : 0;
}

int tb_title_stats_get_uncapped_fps(const tb_title_stats* stats)
{
    if(!stats || stats->total_ms10 == 0)
        return 0;

    return (int)((10000U + (stats->total_ms10 / 2U)) / stats->total_ms10);
}

void tb_title_stats_set_user_time_ms10(tb_title_stats* stats, uint32_t tic_ms10, uint32_t scn_ms10, uint32_t bdr_ms10, uint32_t total_ms10)
{
    if(!stats) return;

    if(stats->tic_ms10 != tic_ms10 ||
       stats->scn_ms10 != scn_ms10 ||
       stats->bdr_ms10 != bdr_ms10 ||
       stats->total_ms10 != total_ms10)
    {
        stats->tic_ms10 = tic_ms10;
        stats->scn_ms10 = scn_ms10;
        stats->bdr_ms10 = bdr_ms10;
        stats->total_ms10 = total_ms10;
        stats->dirty = true;
    }
}

void tb_title_stats_set_lua_perf(tb_title_stats* stats, uint64_t tic_cycles, uint64_t scn_cycles, uint64_t bdr_cycles, uint64_t lua_mem_bytes)
{
    if(!stats) return;

    if(stats->tic_cycles != tic_cycles ||
       stats->scn_cycles != scn_cycles ||
       stats->bdr_cycles != bdr_cycles ||
       stats->lua_mem_bytes != lua_mem_bytes)
    {
        stats->tic_cycles = tic_cycles;
        stats->scn_cycles = scn_cycles;
        stats->bdr_cycles = bdr_cycles;
        stats->lua_mem_bytes = lua_mem_bytes;
        stats->dirty = true;
    }
}

void tb_title_stats_get_title_info(const tb_title_stats* stats, char* out, size_t outcap)
{
    if(!out || outcap == 0) return;
    out[0] = '\0';
    if(!stats) return;

    char msbuf[32];
    uint64_t scn_bdr_cycles = stats->scn_cycles + stats->bdr_cycles;

    tb_format_ms10_value(msbuf, sizeof msbuf, stats->total_ms10);

    snprintf(out, outcap,
        "%d / %d fps | %u + %u = %u kcyc | %s ms | %u kb",
        tb_title_stats_get_fps(stats),
        tb_title_stats_get_uncapped_fps(stats),
        tb_round_kilo(stats->tic_cycles),
        tb_round_kilo(scn_bdr_cycles),
        tb_round_kilo(stats->tic_cycles + scn_bdr_cycles),
        msbuf,
        tb_round_kb(stats->lua_mem_bytes));
}

bool tb_title_stats_take_dirty(tb_title_stats* stats)
{
    if(!stats) return false;
    bool dirty = stats->dirty;
    stats->dirty = false;
    return dirty;
}