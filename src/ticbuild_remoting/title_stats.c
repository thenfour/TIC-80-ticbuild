#include "ticbuild_remoting/title_stats.h"

#include "ticbuild_remoting/utils.h"

#include <stdio.h>

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

    char ticbuf[32], scnbuf[32], totbuf[32];
    char luabuf[32];
    uint64_t scn_bdr_cycles = stats->scn_cycles + stats->bdr_cycles;

    tb_format_kc1(ticbuf, sizeof ticbuf, stats->tic_cycles);
    tb_format_kc1(scnbuf, sizeof scnbuf, scn_bdr_cycles);
    tb_format_kc1(totbuf, sizeof totbuf, stats->tic_cycles + scn_bdr_cycles);
    tb_format_kb1(luabuf, sizeof luabuf, stats->lua_mem_bytes);

    snprintf(out, outcap,
        "FPS: %d | TIC %sk SCN+BDR %sk TOT %sk | LUA %sKB",
        tb_title_stats_get_fps(stats),
        ticbuf, scnbuf, totbuf,
        luabuf);
}

bool tb_title_stats_take_dirty(tb_title_stats* stats)
{
    if(!stats) return false;
    bool dirty = stats->dirty;
    stats->dirty = false;
    return dirty;
}