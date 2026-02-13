#include "ticbuild_remoting/perf_hud.h"

#include "tools.h"
#include "tilesheet.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum
{
    TB_PERF_GRAPH_SPEED_MAX = 8,
    TB_PERF_GRAPH_SPEED_MIN = 1,
    TB_PERF_GRAPH_SPEED_DEFAULT = 5,
    TB_PERF_GRAPH_GAP = 2,
    TB_PERF_VALUE_GAP = 2,
    TB_PERF_VALUE_WIDTH_CHARS = 6,
    TB_PERF_DISPLAY_HZ = 5,
};

enum
{
    TB_PERF_SAMPLE_MIN = 1,
};

static const double TB_PERF_KB_DIV = 1024.0;
static const double TB_PERF_KC_DIV = 1000.0;
static const double TB_PERF_PEAK_DECAY = 0.98;
static const double TB_PERF_MIN_PEAK = 1.0;
static const double TB_PERF_ROUND_EPS = 0.5;

enum
{
    TB_METRIC_FPS = 0,
    TB_METRIC_MEM,
    TB_METRIC_TIC,
    TB_METRIC_SCN_BDR,
    TB_METRIC_CUSTOM0,
    TB_METRIC_CUSTOM1,
    TB_METRIC_CUSTOM2,
    TB_METRIC_CUSTOM3,
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

static uint64_t tb_round_u64(double value)
{
    if(value <= 0.0) return 0;
    return (uint64_t)(value + TB_PERF_ROUND_EPS);
}

static void tb_format_u64(char* out, size_t cap, uint64_t value)
{
    if(!out || cap == 0) return;
    snprintf(out, cap, "%llu", (unsigned long long)value);
}

// static size_t tb_value_width(const char* value)
// {
//     return value ? strlen(value) : 0;
// }

static void tb_pad_left(char* out, size_t cap, const char* value, size_t width)
{
    if(!out || cap == 0) return;
    if(!value) value = "";
    size_t len = strlen(value);
    size_t pad = width > len ? (width - len) : 0;
    size_t needed = pad + len + 1;
    if(needed > cap)
    {
        strncpy(out, value, cap - 1);
        out[cap - 1] = '\0';
        return;
    }

    memset(out, ' ', pad);
    memcpy(out + pad, value, len);
    out[pad + len] = '\0';
}

static int tb_clamp_int(int value, int min_value, int max_value)
{
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

static int tb_abs_int(int value)
{
    return value < 0 ? -value : value;
}

static int tb_luma(const tic_rgb* c)
{
    return c ? (c->r * 3 + c->g * 4 + c->b * 1) : 0;
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

static double tb_graph_sample(const tb_perf_hud_state* state, int metric, uint32_t index)
{
    uint32_t count = state->graph_count[metric];
    if(count == 0)
        return 0.0;

    uint32_t max = (uint32_t)TB_PERF_GRAPH_WIDTH_MAX;
    uint32_t head = state->graph_head[metric];
    uint32_t start = (head + max - count) % max;
    uint32_t idx = (start + index) % max;
    return state->graph_values[metric][idx];
}

static void tb_draw_line(tic_mem* tic, s32 x0, s32 y0, s32 x1, s32 y1, u8 color)
{
    s32 dx = tb_abs_int(x1 - x0);
    s32 sx = x0 < x1 ? 1 : -1;
    s32 dy = -tb_abs_int(y1 - y0);
    s32 sy = y0 < y1 ? 1 : -1;
    s32 err = dx + dy;

    while(true)
    {
        tb_poke4_safe(tic, x0, y0, color);
        if(x0 == x1 && y0 == y1)
            break;

        s32 e2 = err * 2;
        if(e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if(e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static void tb_draw_line_shadow(tic_mem* tic, s32 x0, s32 y0, s32 x1, s32 y1, u8 line_color, u8 shadow_color)
{
    tb_draw_line(tic, x0, y0 + 1, x1, y1 + 1, shadow_color);
    tb_draw_line(tic, x0, y0, x1, y1, line_color);
}

static void tb_draw_graph(tic_mem* tic, const tb_perf_hud_state* state, int metric, s32 x, s32 y, u8 graph_color, u8 shadow_color)
{
    if(!tic || !state) return;

    const s32 width = state->graph_width;
    const s32 height = state->graph_height;
    if(width <= 0 || height <= 0) return;

    uint32_t count = state->graph_count[metric];
    if(count == 0) return;

    double peak = state->graph_peak[metric];
    if(peak < TB_PERF_MIN_PEAK)
        peak = TB_PERF_MIN_PEAK;

    int prev_y = -1;
    int prev_x = -1;
    for(s32 i = 0; i < width; i++)
    {
        uint32_t start = (uint32_t)((uint64_t)i * count / (uint32_t)width);
        uint32_t end = (uint32_t)((uint64_t)(i + 1) * count / (uint32_t)width);
        if(end > 0) end -= 1; // make end inclusive
        if(end < start) end = start;

        double vmin = tb_graph_sample(state, metric, start);
        double vmax = vmin;
        for(uint32_t s = start + 1; s <= end; s++)
        {
            double v = tb_graph_sample(state, metric, s);
            if(v < vmin) vmin = v;
            if(v > vmax) vmax = v;
        }

        double nmax = vmax / peak;
        double nmin = vmin / peak;
        if(nmax < 0.0) nmax = 0.0;
        if(nmax > 1.0) nmax = 1.0;
        if(nmin < 0.0) nmin = 0.0;
        if(nmin > 1.0) nmin = 1.0;

        s32 y_max = y + (height - 1) - (s32)floor(nmax * (double)(height - 1) + 0.5);
        s32 y_min = y + (height - 1) - (s32)floor(nmin * (double)(height - 1) + 0.5);

        if(y_min < y_max)
        {
            s32 tmp = y_min;
            y_min = y_max;
            y_max = tmp;
        }

        s32 px = x + i;
        if(end > start)
        {
            for(s32 py = y_max; py <= y_min; py++)
                tb_poke4_safe(tic, px, py + 1, shadow_color);
            for(s32 py = y_max; py <= y_min; py++)
                tb_poke4_safe(tic, px, py, graph_color);
            prev_y = -1;
            prev_x = -1;
        }
        else
        {
            if(prev_y >= 0 && prev_x >= 0)
            {
                tb_draw_line_shadow(tic, prev_x, prev_y, px, y_max, graph_color, shadow_color);
            }
            else
            {
                tb_poke4_safe(tic, px, y_max + 1, shadow_color);
                tb_poke4_safe(tic, px, y_max, graph_color);
            }
            prev_y = y_max;
            prev_x = px;
        }
    }
}

void ticbuild_perf_hud_init(tb_perf_hud_state* state)
{
    if(!state) return;
    memset(state, 0, sizeof *state);
    tb_fps_init(&state->fps);
    state->graph_speed = TB_PERF_GRAPH_SPEED_DEFAULT;
    state->graph_width = TB_PERF_GRAPH_WIDTH_DEFAULT;
    state->graph_height = TB_PERF_GRAPH_HEIGHT_DEFAULT;
    for(int i = 0; i < TB_PERF_METRIC_COUNT; i++)
        state->graph_peak[i] = 1.0;
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
    state->graph_speed = tb_clamp_int(state->graph_speed + delta, TB_PERF_GRAPH_SPEED_MIN, TB_PERF_GRAPH_SPEED_MAX);
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
    double raw[TB_PERF_METRIC_COUNT];
    raw[TB_METRIC_FPS] = tb_fps_get_value(&state->fps);
    raw[TB_METRIC_MEM] = (double)metrics->lua_mem_bytes / TB_PERF_KB_DIV;
    raw[TB_METRIC_TIC] = (double)metrics->tic_cycles / TB_PERF_KC_DIV;
    raw[TB_METRIC_SCN_BDR] = (double)(metrics->scn_cycles + metrics->bdr_cycles) / TB_PERF_KC_DIV;
    raw[TB_METRIC_CUSTOM0] = (double)metrics->custom[0];
    raw[TB_METRIC_CUSTOM1] = (double)metrics->custom[1];
    raw[TB_METRIC_CUSTOM2] = (double)metrics->custom[2];
    raw[TB_METRIC_CUSTOM3] = (double)metrics->custom[3];

    static const double alphas[TB_PERF_METRIC_COUNT] =
    {
        0.2, // FPS
        0.2, // MEM
        0.25, // TIC
        0.25, // SCN_BDR
        0.25, // CUSTOM0
        0.25, // CUSTOM1
        0.25, // CUSTOM2
        0.25, // CUSTOM3
    };

    for(int i = 0; i < TB_PERF_METRIC_COUNT; i++)
    {
        if(!state->ema_initialized[i])
        {
            state->ema[i] = raw[i];
            state->ema_initialized[i] = true;
        }
        else
        {
            state->ema[i] = tb_ema_update(state->ema[i], raw[i], alphas[i], &state->ema_initialized[i]);
        }
    }

    uint64_t display_interval = 0;
    if(freq > 0)
        display_interval = freq / TB_PERF_DISPLAY_HZ;
    if(display_interval == 0)
        display_interval = TB_PERF_SAMPLE_MIN;

    bool update_display = false;
    if(state->last_display_counter == 0 || counter < state->last_display_counter ||
        (counter - state->last_display_counter) >= display_interval)
    {
        update_display = true;
        state->last_display_counter = counter;
    }

    if(update_display)
    {
        for(int i = 0; i < TB_PERF_METRIC_COUNT; i++)
            state->display[i] = state->ema[i];
    }

    int stride = (TB_PERF_GRAPH_SPEED_MAX + 1 - state->graph_speed);
    state->graph_frame_accum++;
    if(state->graph_frame_accum >= (uint32_t)stride)
    {
        state->graph_frame_accum = 0;
        for(int i = 0; i < TB_PERF_METRIC_COUNT; i++)
        {
            uint32_t head = state->graph_head[i];
            state->graph_values[i][head] = state->ema[i];
            state->graph_head[i] = (head + 1) % TB_PERF_GRAPH_WIDTH_MAX;
            if(state->graph_count[i] < TB_PERF_GRAPH_WIDTH_MAX)
                state->graph_count[i]++;

            if(state->ema[i] > state->graph_peak[i])
                state->graph_peak[i] = state->ema[i];
            else
                state->graph_peak[i] *= TB_PERF_PEAK_DECAY;

            if(state->graph_peak[i] < TB_PERF_MIN_PEAK)
                state->graph_peak[i] = TB_PERF_MIN_PEAK;
        }
    }

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

    int graph_width = state->graph_width;
    int graph_height = state->graph_height;
    if(graph_width <= 0 || graph_width > TB_PERF_GRAPH_WIDTH_MAX)
        graph_width = TB_PERF_GRAPH_WIDTH_DEFAULT;
    if(graph_height <= 0)
        graph_height = TB_PERF_GRAPH_HEIGHT_DEFAULT;

    state->graph_width = graph_width;
    state->graph_height = graph_height;

    const int metrics_full[] =
    {
        TB_METRIC_FPS,
        TB_METRIC_MEM,
        TB_METRIC_TIC,
        TB_METRIC_SCN_BDR,
        TB_METRIC_CUSTOM0,
        TB_METRIC_CUSTOM1,
        TB_METRIC_CUSTOM2,
        TB_METRIC_CUSTOM3,
    };

    const int* metric_list = metrics_full;
    size_t metric_count = COUNT_OF(metrics_full);

    const char* labels[TB_PERF_METRIC_COUNT] =
    {
        "FPS",
        "MEM",
        "TIC",
        "SCN",
        "USR1",
        "USR2",
        "USR3",
        "USR4",
    };

    char value_buf[TB_PERF_METRIC_COUNT][32];
    for(size_t i = 0; i < metric_count; i++)
    {
        int idx = metric_list[i];
        uint64_t value = tb_round_u64(state->display[idx]);
        tb_format_u64(value_buf[idx], sizeof value_buf[idx], value);
    }

    bool draw_graphs = (mode == TB_PERF_HUD_FULL);
    s32 graph_x = 0;
    s32 graph_y = 0;
    s32 value_x = graph_x + graph_width + TB_PERF_GRAPH_GAP;
    s32 label_x = value_x + (s32)TB_PERF_VALUE_WIDTH_CHARS * (s32)tic->ram->font.alt.width + TB_PERF_VALUE_GAP;
    s32 text_offset = (graph_height - tic->ram->font.alt.height) / 2;
    if(text_offset < 0) text_offset = 0;

    for(size_t row = 0; row < metric_count; row++)
    {
        int metric = metric_list[row];
        s32 row_y = graph_y + (s32)row * graph_height;
        s32 text_y = row_y + text_offset;

        if(draw_graphs)
            tb_draw_graph(tic, state, metric, graph_x, row_y, graph_mapped, outline_mapped);

        char padded_value[32];
        tb_pad_left(padded_value, sizeof padded_value, value_buf[metric], TB_PERF_VALUE_WIDTH_CHARS);
        tb_print_outline(tic, padded_value, value_x, text_y, outline_mapped);
        tic_api_print(tic, padded_value, value_x, text_y, (u8)text_color, true, 1, true);

        tb_print_outline(tic, labels[metric], label_x, text_y, outline_mapped);
        tic_api_print(tic, labels[metric], label_x, text_y, (u8)text_color, true, 1, true);
    }
}
