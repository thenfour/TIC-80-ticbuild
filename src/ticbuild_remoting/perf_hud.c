#include "ticbuild_remoting/perf_hud.h"

#include "ticbuild_remoting/lua_perf.h"
#include "ticbuild_remoting/user_timing.h"
#include "ticbuild_remoting/utils.h"

#include "tools.h"

#include <stdio.h>
#include <string.h>

void ticbuild_perf_hud_draw(
    tic_mem* tic,
    tb_fps_tracker* fps,
    uint64_t counter,
    uint64_t freq,
    bool enabled,
    bool run_mode)
{
    if(!enabled || !run_mode)
        return;

    if(!tic || !fps) return;

    tb_fps_on_frame(fps, counter, freq);
    int fps_val = tb_fps_get(fps);

    uint32_t tic_ms10 = 0, scn_ms10 = 0, bdr_ms10 = 0, tot_ms10 = 0;
    uint64_t tic_cycles = 0, scn_cycles = 0, bdr_cycles = 0;
    uint64_t lua_mem_bytes = 0;

    ticbuild_user_timing_end_frame(tic);
    ticbuild_user_timing_get_last_ms10(tic, &tic_ms10, &scn_ms10, &bdr_ms10, &tot_ms10);
    ticbuild_user_timing_get_last_cycles(tic, &tic_cycles, &scn_cycles, &bdr_cycles);
    lua_mem_bytes = ticbuild_lua_perf_get_mem_bytes(tic);

    char ticbuf[32], scnbuf[32], bdrbuf[32], totbuf[32], luabuf[32];
    tb_format_ms10(ticbuf, sizeof ticbuf, tic_ms10);
    tb_format_ms10(scnbuf, sizeof scnbuf, scn_ms10);
    tb_format_ms10(bdrbuf, sizeof bdrbuf, bdr_ms10);
    tb_format_ms10(totbuf, sizeof totbuf, tot_ms10);
    tb_format_kb1(luabuf, sizeof luabuf, lua_mem_bytes);

    char line0[64];
    char line1[128];
    char line2[128];
    char line3[64];

    snprintf(line0, sizeof line0, "FPS: %d", fps_val);
    snprintf(line1, sizeof line1, "TIC %s SCN %s BDR %s TOT %s", ticbuf, scnbuf, bdrbuf, totbuf);
    snprintf(line2, sizeof line2, "VM TIC %llu SCN %llu BDR %llu",
        (unsigned long long)tic_cycles,
        (unsigned long long)scn_cycles,
        (unsigned long long)bdr_cycles);
    snprintf(line3, sizeof line3, "Mem %sKB", luabuf);

    const char* lines[] = {line0, line1, line2, line3};
    const size_t line_count = COUNT_OF(lines);
    size_t maxlen = 0;
    for(size_t i = 0; i < line_count; i++)
    {
        size_t len = strlen(lines[i]);
        if(len > maxlen) maxlen = len;
    }

    enum {Padding = 2, LineHeight = TIC_FONT_HEIGHT + 1};
    s32 w = (s32)(maxlen * TIC_FONT_WIDTH + Padding * 2);
    s32 h = (s32)(line_count * LineHeight + Padding * 2);
    s32 x = 2;
    s32 y = 2;

    tic_api_rect(tic, x - 1, y - 1, w + 2, h + 2, tic_color_dark_grey);
    tic_api_rect(tic, x, y, w, h, tic_color_black);

    for(size_t i = 0; i < line_count; i++)
    {
        s32 ty = y + Padding + (s32)i * LineHeight;
        tic_api_print(tic, lines[i], x + Padding, ty, tic_color_white, true, 1, true);
    }
}
