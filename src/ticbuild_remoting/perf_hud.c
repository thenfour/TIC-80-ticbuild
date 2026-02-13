#include "ticbuild_remoting/perf_hud.h"

#include "ticbuild_remoting/lua_perf.h"
#include "ticbuild_remoting/user_timing.h"
#include "ticbuild_remoting/utils.h"

#include "tools.h"
#include "tilesheet.h"

#include <stdio.h>
#include <string.h>

static inline void tb_poke4_safe(tic_mem* tic, s32 x, s32 y, u8 color)
{
    if(x < 0 || y < 0 || x >= TIC80_WIDTH || y >= TIC80_HEIGHT) return;
    tic_api_poke4(tic, y * TIC80_WIDTH + x, color);
}

static void tb_print_outline(tic_mem* tic, const char* text, s32 x, s32 y, tic_color color)
{
    if(!tic || !text) return;

    const tic_font_data* font = &tic->ram->font.alt;
    s32 width = font->width;
    s32 height = font->height;

    const u8 outline = tic_tool_peek4(tic->ram->vram.mapping, tic_color_black & 0xf);
    const u8 fill = tic_tool_peek4(tic->ram->vram.mapping, color & 0xf);

    tic_tilesheet font_sheet = tic_tilesheet_get(1, (u8*)&tic->ram->font);

    s32 start_x = x;
    while(*text)
    {
        char sym = *text++;
        if(sym == '\n')
        {
            x = start_x;
            y += height;
            continue;
        }

        tic_tileptr font_char = tic_tilesheet_gettile(&font_sheet, TIC_FONT_CHARS + (unsigned char)sym, true);

        for(s32 py = 0; py < TIC_SPRITESIZE; py++)
        {
            for(s32 px = 0; px < TIC_SPRITESIZE; px++)
            {
                if(tic_tilesheet_gettilepix(&font_char, px, py) == 0)
                    continue;

                s32 dx = x + px;
                s32 dy = y + py;

                tb_poke4_safe(tic, dx - 1, dy, outline);
                tb_poke4_safe(tic, dx + 1, dy, outline);
                tb_poke4_safe(tic, dx, dy - 1, outline);
                tb_poke4_safe(tic, dx, dy + 1, outline);

                tb_poke4_safe(tic, dx - 1, dy - 1, outline);
                tb_poke4_safe(tic, dx + 1, dy - 1, outline);
                tb_poke4_safe(tic, dx - 1, dy + 1, outline);
                tb_poke4_safe(tic, dx + 1, dy + 1, outline);

                //tb_poke4_safe(tic, dx, dy, fill);
            }
        }

        x += width;
    }
}

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

    uint32_t tic_ms10 = 0, scn_ms10 = 0, bdr_ms10 = 0;
    uint64_t tic_cycles = 0, scn_cycles = 0, bdr_cycles = 0;
    uint64_t lua_mem_bytes = 0;

    ticbuild_user_timing_end_frame(tic);
    ticbuild_user_timing_get_last_ms10(tic, &tic_ms10, &scn_ms10, &bdr_ms10, NULL);
    ticbuild_user_timing_get_last_cycles(tic, &tic_cycles, &scn_cycles, &bdr_cycles);
    lua_mem_bytes = ticbuild_lua_perf_get_mem_bytes(tic);

    char ticbuf[32], scnbuf[32], bdrbuf[32], luabuf[32];
    tb_format_ms10_value(ticbuf, sizeof ticbuf, tic_ms10);
    tb_format_ms10_value(scnbuf, sizeof scnbuf, scn_ms10);
    tb_format_ms10_value(bdrbuf, sizeof bdrbuf, bdr_ms10);
    tb_format_kb1(luabuf, sizeof luabuf, lua_mem_bytes);

    char line0[64];
    char line1[64];
    char line2[64];
    char line3[64];
    char line4[64];
    char line5[64];
    char line6[64];
    char line7[64];

    snprintf(line0, sizeof line0, "FPS      %d", fps_val);
    snprintf(line1, sizeof line1, "Mem kb   %s", luabuf);
    snprintf(line2, sizeof line2, "TIC ms   %s", ticbuf);
    snprintf(line3, sizeof line3, "SCN ms   %s", scnbuf);
    snprintf(line4, sizeof line4, "BDR ms   %s", bdrbuf);
    snprintf(line5, sizeof line5, "TIC kcyl %llu", (unsigned long long)(tic_cycles / 1000));
    snprintf(line6, sizeof line6, "SCN kcyl %llu", (unsigned long long)(scn_cycles / 1000));
    snprintf(line7, sizeof line7, "BDR kcyl %llu", (unsigned long long)(bdr_cycles / 1000));

    const char* lines[] = {line0, line1, line2, line3, line4, line5, line6, line7};
    const size_t line_count = COUNT_OF(lines);

    enum {Padding = 2, LineHeight = TIC_FONT_HEIGHT + 1};
    s32 x = 2;
    s32 y = 2;

    for(size_t i = 0; i < line_count; i++)
    {
        s32 ty = y + Padding + (s32)i * LineHeight;
        tb_print_outline(tic, lines[i], x + Padding, ty, tic_color_white);
        tic_api_print(tic, lines[i], x + Padding, ty, tic_color_white, true, 1, true);
    }
}
