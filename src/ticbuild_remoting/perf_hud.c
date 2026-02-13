#include "ticbuild_remoting/perf_hud.h"

#include "tools.h"
#include "tilesheet.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
    TB_PERF_GRAPH_RANGE_ADJUST = 1,
    TB_PERF_LABEL_MAX = 31,
};

enum
{
    TB_PERF_SAMPLE_MIN = 1,
    TB_PERF_CUSTOM_SLOTS = 4,
    TB_PERF_BUILTIN_METRICS = 4,
};

static const double TB_PERF_KB_DIV = 1024.0;
static const double TB_PERF_KC_DIV = 1000.0;
static const double TB_PERF_PEAK_DECAY = 0.98;
static const double TB_PERF_MIN_PEAK = 1.0;
static const double TB_PERF_ROUND_EPS = 0.5;
static const double TB_PERF_ALPHA_FPS = 0.2;
static const double TB_PERF_ALPHA_MEM = 0.2;
static const double TB_PERF_ALPHA_DEFAULT = 0.25;
static const double TB_PERF_ALPHA_MIN = 0.0;
static const double TB_PERF_ALPHA_MAX = 1.0;

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

typedef enum
{
    TB_SEVERITY_OK = 0,
    TB_SEVERITY_WARNING,
    TB_SEVERITY_ALERT,
} tb_perf_severity;

enum
{
    TB_HEX_SHORT_LEN = 3,
    TB_HEX_FULL_LEN = 6,
};

static const tic_rgb TB_COLOR_TEXT_DEFAULT = {255, 255, 255};
static const tic_rgb TB_COLOR_OUTLINE_DEFAULT = {0, 0, 0};
static const tic_rgb TB_COLOR_OK_DEFAULT = {0, 220, 90};
static const tic_rgb TB_COLOR_WARNING_DEFAULT = {255, 160, 0};
static const tic_rgb TB_COLOR_ALERT_DEFAULT = {255, 40, 40};

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

static double tb_clamp_double(double value, double min_value, double max_value)
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

static int tb_hex_nibble(char ch)
{
    if(ch >= '0' && ch <= '9') return ch - '0';
    if(ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if(ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

static bool tb_is_ascii_whitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static bool tb_is_decimal_token(const char* value)
{
    if(!value || !value[0]) return false;
    for(const char* p = value; *p; p++)
    {
        if(*p < '0' || *p > '9')
            return false;
    }
    return true;
}

static int tb_parse_palette_index(const char* value)
{
    if(!tb_is_decimal_token(value))
        return -1;

    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if(end == value || (end && *end))
        return -1;

    if(parsed < 0 || parsed >= TIC_PALETTE_SIZE)
        return -1;

    return (int)parsed;
}

static bool tb_parse_hex_rgb(const char* value, tic_rgb* out)
{
    if(!value || !out) return false;
    size_t len = strlen(value);
    if(len != TB_HEX_SHORT_LEN && len != TB_HEX_FULL_LEN)
        return false;

    if(len == TB_HEX_SHORT_LEN)
    {
        int r = tb_hex_nibble(value[0]);
        int g = tb_hex_nibble(value[1]);
        int b = tb_hex_nibble(value[2]);
        if(r < 0 || g < 0 || b < 0)
            return false;

        out->r = (u8)((r << 4) | r);
        out->g = (u8)((g << 4) | g);
        out->b = (u8)((b << 4) | b);
        return true;
    }

    int r1 = tb_hex_nibble(value[0]);
    int r2 = tb_hex_nibble(value[1]);
    int g1 = tb_hex_nibble(value[2]);
    int g2 = tb_hex_nibble(value[3]);
    int b1 = tb_hex_nibble(value[4]);
    int b2 = tb_hex_nibble(value[5]);
    if(r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0)
        return false;

    out->r = (u8)((r1 << 4) | r2);
    out->g = (u8)((g1 << 4) | g2);
    out->b = (u8)((b1 << 4) | b2);
    return true;
}

static bool tb_equals_ignore_case(const char* a, const char* b)
{
    if(!a || !b) return false;
    while(*a && *b)
    {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void tb_trim_ascii(char* value)
{
    if(!value) return;

    size_t len = strlen(value);
    size_t start = 0;
    while(start < len && tb_is_ascii_whitespace(value[start]))
        start++;

    size_t end = len;
    while(end > start && tb_is_ascii_whitespace(value[end - 1]))
        end--;

    if(start > 0 && end > start)
        memmove(value, value + start, end - start);

    value[end - start] = '\0';
}

static void tb_set_color_spec_default(tb_perf_hud_color_spec* spec, const tic_rgb* target)
{
    if(!spec || !target) return;
    spec->enabled = false;
    spec->palette_index = -1;
    spec->use_auto = true;
    spec->target = *target;
}

static void tb_parse_color_spec(const char* value, const tic_rgb* fallback, tb_perf_hud_color_spec* spec)
{
    if(!spec || !fallback) return;

    tb_set_color_spec_default(spec, fallback);
    if(!value || !value[0])
        return;

    char token[64];
    strncpy(token, value, sizeof token - 1);
    token[sizeof token - 1] = '\0';
    tb_trim_ascii(token);
    if(token[0] == '\0')
        return;

    spec->enabled = true;

    if(tb_equals_ignore_case(token, "auto"))
    {
        spec->use_auto = true;
        spec->target = *fallback;
        return;
    }

    int palette_index = tb_parse_palette_index(token);
    if(palette_index >= 0)
    {
        spec->use_auto = false;
        spec->palette_index = palette_index;
        return;
    }

    tic_rgb parsed = {0};
    if(tb_parse_hex_rgb(token, &parsed))
    {
        spec->use_auto = true;
        spec->target = parsed;
        return;
    }

    tb_set_color_spec_default(spec, fallback);
}

static int tb_pick_palette_for_target(tic_mem* tic, const tic_rgb* target)
{
    if(!tic || !target) return 0;

    int best = 0;
    int best_dist = 0;

    for(int i = 0; i < TIC_PALETTE_SIZE; i++)
    {
        u8 mapped = tb_map_color(tic, i);
        tic_rgb color = tic->ram->vram.palette.colors[mapped];
        int dist = tb_rgb_dist_sq(&color, target);
        if(i == 0 || dist < best_dist)
        {
            best_dist = dist;
            best = i;
        }
    }

    return best;
}

static int tb_resolve_color_spec(tic_mem* tic, const tb_perf_hud_color_spec* spec)
{
    if(!spec)
        return 0;

    if(!spec->use_auto)
        return tb_clamp_int(spec->palette_index, 0, TIC_PALETTE_SIZE - 1);

    return tb_pick_palette_for_target(tic, &spec->target);
}

static tb_perf_severity tb_classify_metric(const tb_perf_hud_state* state, int metric, double value)
{
    if(!state)
        return TB_SEVERITY_OK;

    if(metric >= TB_METRIC_CUSTOM0)
        return TB_SEVERITY_OK;

    const tb_perf_hud_thresholds* t = &state->thresholds;

    if(metric == TB_METRIC_FPS)
    {
        if(value < (double)t->fps_alert)
            return TB_SEVERITY_ALERT;

        if(t->fps_warn > t->fps_alert && value < (double)t->fps_warn)
            return TB_SEVERITY_WARNING;

        return TB_SEVERITY_OK;
    }

    if(metric == TB_METRIC_MEM)
    {
        if(value > (double)t->mem_alert_kb)
            return TB_SEVERITY_ALERT;

        if(t->mem_warn_kb < t->mem_alert_kb && value > (double)t->mem_warn_kb)
            return TB_SEVERITY_WARNING;

        return TB_SEVERITY_OK;
    }

    if(value > (double)t->cycles_alert_kc)
        return TB_SEVERITY_ALERT;

    if(t->cycles_warn_kc < t->cycles_alert_kc && value > (double)t->cycles_warn_kc)
        return TB_SEVERITY_WARNING;

    return TB_SEVERITY_OK;
}

static u8 tb_graph_color_for_severity(tb_perf_severity severity, u8 ok, u8 warning, u8 alert)
{
    if(severity == TB_SEVERITY_ALERT) return alert;
    if(severity == TB_SEVERITY_WARNING) return warning;
    return ok;
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

static void tb_draw_graph(
    tic_mem* tic,
    const tb_perf_hud_state* state,
    int metric,
    s32 x,
    s32 y,
    u8 graph_ok_color,
    u8 graph_warning_color,
    u8 graph_alert_color,
    u8 shadow_color)
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
        if(end > 0) end -= TB_PERF_GRAPH_RANGE_ADJUST; // make end inclusive
        if(end < start) end = start;

        double vmin = tb_graph_sample(state, metric, start);
        double vmax = vmin;
        tb_perf_severity severity = tb_classify_metric(state, metric, vmin);
        for(uint32_t s = start + 1; s <= end; s++)
        {
            double v = tb_graph_sample(state, metric, s);
            if(v < vmin) vmin = v;
            if(v > vmax) vmax = v;

            tb_perf_severity sample_severity = tb_classify_metric(state, metric, v);
            if(sample_severity > severity)
                severity = sample_severity;
        }

        u8 graph_color = tb_graph_color_for_severity(severity, graph_ok_color, graph_warning_color, graph_alert_color);

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
    tb_set_color_spec_default(&state->palette_text, &TB_COLOR_TEXT_DEFAULT);
    tb_set_color_spec_default(&state->palette_outline, &TB_COLOR_OUTLINE_DEFAULT);
    tb_set_color_spec_default(&state->palette_ok, &TB_COLOR_OK_DEFAULT);
    tb_set_color_spec_default(&state->palette_warning, &TB_COLOR_WARNING_DEFAULT);
    tb_set_color_spec_default(&state->palette_alert, &TB_COLOR_ALERT_DEFAULT);

    state->thresholds.fps_warn = 57;
    state->thresholds.fps_alert = 53;
    state->thresholds.mem_warn_kb = 102400;
    state->thresholds.mem_alert_kb = 256000;
    state->thresholds.cycles_warn_kc = 1800;
    state->thresholds.cycles_alert_kc = 2400;
}

void ticbuild_perf_hud_set_palette_override(
    tb_perf_hud_state* state,
    const char* text,
    const char* outline,
    const char* ok,
    const char* warning,
    const char* alert)
{
    if(!state) return;
    tb_parse_color_spec(text, &TB_COLOR_TEXT_DEFAULT, &state->palette_text);
    tb_parse_color_spec(outline, &TB_COLOR_OUTLINE_DEFAULT, &state->palette_outline);
    tb_parse_color_spec(ok, &TB_COLOR_OK_DEFAULT, &state->palette_ok);
    tb_parse_color_spec(warning, &TB_COLOR_WARNING_DEFAULT, &state->palette_warning);
    tb_parse_color_spec(alert, &TB_COLOR_ALERT_DEFAULT, &state->palette_alert);
}

void ticbuild_perf_hud_set_thresholds(
    tb_perf_hud_state* state,
    int fps_warn,
    int fps_alert,
    int mem_warn_kb,
    int mem_alert_kb,
    int cycles_warn_kc,
    int cycles_alert_kc)
{
    if(!state) return;

    state->thresholds.fps_warn = fps_warn;
    state->thresholds.fps_alert = fps_alert;
    state->thresholds.mem_warn_kb = mem_warn_kb;
    state->thresholds.mem_alert_kb = mem_alert_kb;
    state->thresholds.cycles_warn_kc = cycles_warn_kc;
    state->thresholds.cycles_alert_kc = cycles_alert_kc;
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

    double alphas[TB_PERF_METRIC_COUNT] =
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

    for(int i = 0; i < TB_PERF_CUSTOM_SLOTS; i++)
    {
        int metric = TB_METRIC_CUSTOM0 + i;
        alphas[metric] = tb_clamp_double(metrics->custom_alpha[i], TB_PERF_ALPHA_MIN, TB_PERF_ALPHA_MAX);
    }

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

    int text_color = tb_resolve_color_spec(tic, &state->palette_text);
    int outline_color = tb_resolve_color_spec(tic, &state->palette_outline);
    int ok_color = tb_resolve_color_spec(tic, &state->palette_ok);
    int warning_color = tb_resolve_color_spec(tic, &state->palette_warning);
    int alert_color = tb_resolve_color_spec(tic, &state->palette_alert);

    u8 outline_mapped = tb_map_color(tic, outline_color);
    u8 graph_ok_mapped = tb_map_color(tic, ok_color);
    u8 graph_warning_mapped = tb_map_color(tic, warning_color);
    u8 graph_alert_mapped = tb_map_color(tic, alert_color);

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
    };

    int metric_list[TB_PERF_METRIC_COUNT];
    size_t metric_count = 0;
    for(size_t i = 0; i < COUNT_OF(metrics_full); i++)
        metric_list[metric_count++] = metrics_full[i];

    for(int i = 0; i < TB_PERF_CUSTOM_SLOTS; i++)
    {
        if(metrics->custom_active[i])
            metric_list[metric_count++] = TB_METRIC_CUSTOM0 + i;
    }

    const char* labels_builtin[TB_PERF_BUILTIN_METRICS] =
    {
        "FPS",
        "MEM",
        "TIC",
        "SCN",
    };

    char labels_custom[TB_PERF_CUSTOM_SLOTS][TB_PERF_LABEL_MAX + 1];
    for(int i = 0; i < TB_PERF_CUSTOM_SLOTS; i++)
    {
        if(metrics->custom_label[i][0])
        {
            strncpy(labels_custom[i], metrics->custom_label[i], TB_PERF_LABEL_MAX);
            labels_custom[i][TB_PERF_LABEL_MAX] = '\0';
        }
        else
        {
            snprintf(labels_custom[i], sizeof labels_custom[i], "USR%d", i + 1);
        }
    }

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
            tb_draw_graph(
                tic,
                state,
                metric,
                graph_x,
                row_y,
                graph_ok_mapped,
                graph_warning_mapped,
                graph_alert_mapped,
                outline_mapped);

        char padded_value[32];
        tb_pad_left(padded_value, sizeof padded_value, value_buf[metric], TB_PERF_VALUE_WIDTH_CHARS);
        tb_print_outline(tic, padded_value, value_x, text_y, outline_mapped);
        tb_perf_severity value_severity = tb_classify_metric(state, metric, state->display[metric]);
        int value_color = ok_color;
        if(value_severity == TB_SEVERITY_WARNING) value_color = warning_color;
        if(value_severity == TB_SEVERITY_ALERT) value_color = alert_color;
        tic_api_print(tic, padded_value, value_x, text_y, (u8)value_color, true, 1, true);

        const char* label = metric < TB_PERF_BUILTIN_METRICS
            ? labels_builtin[metric]
            : labels_custom[metric - TB_METRIC_CUSTOM0];

        tb_print_outline(tic, label, label_x, text_y, outline_mapped);
        tic_api_print(tic, label, label_x, text_y, (u8)text_color, true, 1, true);
    }
}
