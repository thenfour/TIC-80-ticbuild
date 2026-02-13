#include "ticbuild_remoting/perf_hud.h"

#include "ticbuild_remoting/utils.h"

#include "tools.h"
#include "tilesheet.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum
{
    TB_PERF_GRAPH_WIDTH = 188,
    TB_PERF_GRAPH_HEIGHT = 15,
    TB_PERF_GRAPH_X = 2,
    TB_PERF_GRAPH_Y = 2,
    TB_PERF_GRAPH_SPEED_MAX = 8,
};

static inline u8 tb_map_color(const tic_mem* tic, int color)
{
    if(!tic) return 0;
    return tic_tool_peek4(tic->ram->vram.mapping, (u8)(color & 0xf));
}

static inline void tb_poke4_safe(tic_mem* tic, s32 x, s32 y, u8 color)
{
    if(x < 0 || y < 0 || x >= TIC80_WIDTH || y >= TIC80_HEIGHT) return;
    tic_api_poke4(tic, y * TIC80_WIDTH + x, color);
}

static double tb_ema_update(double prev, double value, double alpha, bool* initialized)
{
    if(!initialized || !(*initialized))
        return value;

    return prev + alpha * (value - prev);
}

static void tb_format_float1(char* out, size_t cap, double value)
{
    if(!out || cap == 0) return;
    snprintf(out, cap, "%.1f", value);
    tb_trim_trailing_zeros(out);
}

static int tb_clamp_int(int value, int min_value, int max_value)
{
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

static int tb_luma(const tic_rgb* c)
{
    return c ? (c->r * 3 + c->g * 4 + c->b) : 0;
}

static int tb_rgb_dist_sq(const tic_rgb* a, const tic_rgb* b)
{
    int dr = (int)a->r - (int)b->r;
    int dg = (int)a->g - (int)b->g;
    int db = (int)a->b - (int)b->b;
    return dr * dr + dg * dg + db * db;
}

static void tb_pick_palette(tic_mem* tic, int* out_text, int* out_outline, int* out_graph)
{
    tic_rgb colors[TIC_PALETTE_SIZE];
    for(int i = 0; i < TIC_PALETTE_SIZE; i++)
    {
        u8 mapped = tb_map_color(tic, i);
        colors[i] = tic->ram->vram.palette.colors[mapped];
    }

    int best_a = 0;
    int best_b = 1;
    int best_dist = -1;
    for(int i = 0; i < TIC_PALETTE_SIZE; i++)
    {
        for(int j = i + 1; j < TIC_PALETTE_SIZE; j++)
        {
            int dist = tb_rgb_dist_sq(&colors[i], &colors[j]);
            if(dist > best_dist)
            {
                best_dist = dist;
                best_a = i;
                best_b = j;
            }
        }
    }

    int text = best_a;
    int outline = best_b;
    if(tb_luma(&colors[text]) < tb_luma(&colors[outline]))
    {
        int tmp = text;
        text = outline;
        outline = tmp;
    }

    int graph = text;
    int graph_score = -1;
    for(int i = 0; i < TIC_PALETTE_SIZE; i++)
    {
        if(i == text || i == outline) continue;
        int dist_text = tb_rgb_dist_sq(&colors[i], &colors[text]);
        int dist_outline = tb_rgb_dist_sq(&colors[i], &colors[outline]);
        int score = dist_text < dist_outline ? dist_text : dist_outline;
        if(score > graph_score)
        {
            graph_score = score;
            graph = i;
        }
    }

    if(out_text) *out_text = text;
    if(out_outline) *out_outline = outline;
    if(out_graph) *out_graph = graph;
}

static void tb_print_outline(tic_mem* tic, const char* text, s32 x, s32 y, u8 outline_color)
{
    if(!tic || !text) return;

    const tic_font_data* font = &tic->ram->font.alt;
    s32 width = font->width;
    s32 height = font->height;

    const u8 outline = outline_color;

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
            }
        }

        x += width;
    }
}

static void tb_draw_graph(tic_mem* tic, const tb_perf_hud_state* state, s32 x, s32 y, u8 graph_color, u8 outline_color)
{
    if(!tic || !state) return;

    const s32 width = TB_PERF_GRAPH_WIDTH;
    const s32 height = TB_PERF_GRAPH_HEIGHT;

    const s32 left = x;
    const s32 top = y;
    const s32 right = x + width - 1;
    const s32 bottom = y + height - 1;
    const s32 inner_top = top + 1;
    const s32 inner_bottom = bottom - 1;

    for(s32 px = left; px <= right; px++)
    {
        tb_poke4_safe(tic, px, top, outline_color);
        tb_poke4_safe(tic, px, bottom, outline_color);
    }

    for(s32 py = top; py <= bottom; py++)
    {
        tb_poke4_safe(tic, left, py, outline_color);
        tb_poke4_safe(tic, right, py, outline_color);
    }

    double peak = state->graph_peak;
    if(peak < 1.0)
        peak = 1.0;

    for(s32 i = 0; i < width; i++)
    {
        uint32_t idx = (state->graph_head + (uint32_t)i) % TB_PERF_GRAPH_WIDTH;
        double value = state->graph_values[idx];
        double norm = value / peak;
        if(norm < 0.0) norm = 0.0;
        if(norm > 1.0) norm = 1.0;
        int bar = (int)floor(norm * (double)(height - 3) + 0.5);
        s32 px = x + i;
        s32 py = inner_bottom - bar;
        if(py < inner_top) py = inner_top;

        for(s32 yy = py; yy <= inner_bottom; yy++)
            tb_poke4_safe(tic, px, yy, graph_color);
    }
}

void ticbuild_perf_hud_init(tb_perf_hud_state* state)
{
    if(!state) return;
    memset(state, 0, sizeof *state);
    tb_fps_init(&state->fps);
    state->graph_speed = 5;
    state->graph_peak = 1.0;
    state->palette_text = -1;
    state->palette_outline = -1;
    state->palette_graph = -1;
}

void ticbuild_perf_hud_set_palette_override(tb_perf_hud_state* state, int text, int outline, int graph)
{
    if(!state) return;
    state->palette_text = (text >= 0 && text < TIC_PALETTE_SIZE) ? text : -1;
    state->palette_outline = (outline >= 0 && outline < TIC_PALETTE_SIZE) ? outline : -1;
    state->palette_graph = (graph >= 0 && graph < TIC_PALETTE_SIZE) ? graph : -1;
}

void ticbuild_perf_hud_adjust_graph_speed(tb_perf_hud_state* state, int delta)
{
    if(!state) return;
    state->graph_speed = tb_clamp_int(state->graph_speed + delta, 1, TB_PERF_GRAPH_SPEED_MAX);
}

int ticbuild_perf_hud_get_graph_speed(const tb_perf_hud_state* state)
{
    return state ? state->graph_speed : 0;
}

void ticbuild_perf_hud_draw(
    tic_mem* tic,
    tb_perf_hud_state* state,
    const tb_perf_metrics* metrics,
    uint64_t counter,
    uint64_t freq,
    tb_perf_hud_mode mode,
    bool run_mode)
{
    if(!tic || !state || !metrics) return;
    if(!run_mode) return;

    tb_fps_on_frame(&state->fps, counter, freq);
    double fps_value = tb_fps_get_value(&state->fps);

    double mem_kb = (double)metrics->lua_mem_bytes / 1024.0;
    double tic_kc = (double)metrics->tic_cycles / 1000.0;
    double scn_bdr_kc = (double)(metrics->scn_cycles + metrics->bdr_cycles) / 1000.0;
    double total_kc = tic_kc + scn_bdr_kc;

    if(!state->ema_initialized)
    {
        state->fps_ema = fps_value;
        state->mem_kb_ema = mem_kb;
        state->tic_kc_ema = tic_kc;
        state->scn_bdr_kc_ema = scn_bdr_kc;
        state->total_kc_ema = total_kc;
        state->ema_initialized = true;
    }
    else
    {
        state->fps_ema = tb_ema_update(state->fps_ema, fps_value, 0.2, &state->ema_initialized);
        state->mem_kb_ema = tb_ema_update(state->mem_kb_ema, mem_kb, 0.2, &state->ema_initialized);
        state->tic_kc_ema = tb_ema_update(state->tic_kc_ema, tic_kc, 0.25, &state->ema_initialized);
        state->scn_bdr_kc_ema = tb_ema_update(state->scn_bdr_kc_ema, scn_bdr_kc, 0.25, &state->ema_initialized);
        state->total_kc_ema = tb_ema_update(state->total_kc_ema, total_kc, 0.25, &state->ema_initialized);
    }

    uint64_t display_interval = 0;
    if(freq > 0)
        display_interval = freq / 5;
    if(display_interval == 0)
        display_interval = 1;

    bool update_display = false;
    if(state->last_display_counter == 0 || counter < state->last_display_counter ||
        (counter - state->last_display_counter) >= display_interval)
    {
        update_display = true;
        state->last_display_counter = counter;
    }

    if(update_display)
    {
        state->fps_display = state->fps_ema;
        state->mem_kb_display = state->mem_kb_ema;
        state->tic_kc_display = state->tic_kc_ema;
        state->scn_bdr_kc_display = state->scn_bdr_kc_ema;
        state->total_kc_display = state->total_kc_ema;
    }

    int stride = (TB_PERF_GRAPH_SPEED_MAX + 1 - state->graph_speed);
    state->graph_frame_accum++;
    if(state->graph_frame_accum >= (uint32_t)stride)
    {
        state->graph_frame_accum = 0;
        state->graph_values[state->graph_head] = state->total_kc_ema;
        state->graph_head = (state->graph_head + 1) % TB_PERF_GRAPH_WIDTH;

        if(state->total_kc_ema > state->graph_peak)
            state->graph_peak = state->total_kc_ema;
        else
            state->graph_peak *= 0.98;
    }

    if(state->graph_peak < 1.0)
        state->graph_peak = 1.0;

    if(mode == TB_PERF_HUD_OFF)
        return;

    int text_color = tic_color_white;
    int outline_color = tic_color_black;
    int graph_color = tic_color_light_grey;
    tb_pick_palette(tic, &text_color, &outline_color, &graph_color);

    if(state->palette_text >= 0) text_color = state->palette_text;
    if(state->palette_outline >= 0) outline_color = state->palette_outline;
    if(state->palette_graph >= 0) graph_color = state->palette_graph;

    u8 outline_mapped = tb_map_color(tic, outline_color);
    u8 graph_mapped = tb_map_color(tic, graph_color);

    tb_draw_graph(tic, state, TB_PERF_GRAPH_X, TB_PERF_GRAPH_Y, graph_mapped, outline_mapped);

    char fpsbuf[32];
    char membuf[32];
    char ticbuf[32];
    char scnbuf[32];
    char totbuf[32];

    tb_format_float1(fpsbuf, sizeof fpsbuf, state->fps_display);
    tb_format_float1(membuf, sizeof membuf, state->mem_kb_display);
    tb_format_float1(ticbuf, sizeof ticbuf, state->tic_kc_display);
    tb_format_float1(scnbuf, sizeof scnbuf, state->scn_bdr_kc_display);
    tb_format_float1(totbuf, sizeof totbuf, state->total_kc_display);

    char line0[64];
    char line1[64];
    char line2[64];
    char line3[64];
    char line4[64];

    snprintf(line0, sizeof line0, "FPS      %s", fpsbuf);
    snprintf(line1, sizeof line1, "MEM KB   %s", membuf);
    snprintf(line2, sizeof line2, "TIC KCY  %s", ticbuf);
    snprintf(line3, sizeof line3, "SCN+BDR  %s", scnbuf);
    snprintf(line4, sizeof line4, "TOTAL    %s", totbuf);

    const char* lines_full[] = {line0, line1, line2, line3, line4};
    const char* lines_min[] = {line0, line4};

    const char** lines = lines_full;
    size_t line_count = COUNT_OF(lines_full);
    if(mode == TB_PERF_HUD_MINIMAL)
    {
        lines = lines_min;
        line_count = COUNT_OF(lines_min);
    }

    enum {Padding = 2, LineHeight = TIC_FONT_HEIGHT + 1};
    s32 x = TB_PERF_GRAPH_X;
    s32 y = TB_PERF_GRAPH_Y + TB_PERF_GRAPH_HEIGHT + 2;

    for(size_t i = 0; i < line_count; i++)
    {
        s32 ty = y + Padding + (s32)i * LineHeight;
        tb_print_outline(tic, lines[i], x + Padding, ty, outline_mapped);
        tic_api_print(tic, lines[i], x + Padding, ty, (u8)text_color, true, 1, true);
    }
}
