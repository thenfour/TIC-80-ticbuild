#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "api.h"
#include "ticbuild_remoting/fps.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        TB_PERF_METRIC_COUNT = 8,
        TB_PERF_GRAPH_WIDTH_MAX = 240,
        TB_PERF_GRAPH_WIDTH_DEFAULT = 188,
        TB_PERF_GRAPH_HEIGHT_DEFAULT = 15,
    };

    typedef enum
    {
        TB_PERF_HUD_OFF = 0,
        TB_PERF_HUD_FULL,
        TB_PERF_HUD_MINIMAL,
    } tb_perf_hud_mode;

    typedef struct
    {
        uint64_t tic_cycles;
        uint64_t scn_cycles;
        uint64_t bdr_cycles;
        uint64_t lua_mem_bytes;
        double custom[4];
        bool custom_active[4];
        double custom_alpha[4];
        char custom_label[4][32];
    } tb_perf_metrics;

    typedef struct
    {
        tb_fps_tracker fps;

        double ema[TB_PERF_METRIC_COUNT];
        double display[TB_PERF_METRIC_COUNT];

        double graph_values[TB_PERF_METRIC_COUNT][TB_PERF_GRAPH_WIDTH_MAX];
        double graph_peak[TB_PERF_METRIC_COUNT];
        uint32_t graph_head[TB_PERF_METRIC_COUNT];
        uint32_t graph_count[TB_PERF_METRIC_COUNT];
        uint32_t graph_frame_accum;
        int graph_speed;
        int graph_width;
        int graph_height;

        uint64_t last_display_counter;
        bool ema_initialized[TB_PERF_METRIC_COUNT];

        int palette_text;
        int palette_outline;
        int palette_graph;
    } tb_perf_hud_state;

    void ticbuild_perf_hud_init(tb_perf_hud_state* state);
    void ticbuild_perf_hud_set_palette_override(tb_perf_hud_state* state, int text, int outline, int graph);
    void ticbuild_perf_hud_adjust_graph_speed(tb_perf_hud_state* state, int delta);
    int ticbuild_perf_hud_get_graph_speed(const tb_perf_hud_state* state);

    void ticbuild_perf_hud_draw(
        tic_mem* tic,
        tb_perf_hud_state* state,
        const tb_perf_metrics* metrics,
        uint64_t counter,
        uint64_t freq,
        tb_perf_hud_mode mode,
        bool run_mode);

#ifdef __cplusplus
}
#endif
