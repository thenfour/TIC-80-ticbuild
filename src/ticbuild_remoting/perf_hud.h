#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "api.h"
#include "ticbuild_remoting/fps.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        TB_PERF_HUD_OFF = 0,
        TB_PERF_HUD_MINIMAL,
        TB_PERF_HUD_FULL,
    } tb_perf_hud_mode;

    typedef struct
    {
        uint64_t tic_cycles;
        uint64_t scn_cycles;
        uint64_t bdr_cycles;
        uint64_t lua_mem_bytes;
    } tb_perf_metrics;

    typedef struct
    {
        tb_fps_tracker fps;

        double fps_ema;
        double mem_kb_ema;
        double tic_kc_ema;
        double scn_bdr_kc_ema;
        double total_kc_ema;

        double fps_display;
        double mem_kb_display;
        double tic_kc_display;
        double scn_bdr_kc_display;
        double total_kc_display;

        double graph_values[188];
        double graph_peak;
        uint32_t graph_head;
        uint32_t graph_frame_accum;
        int graph_speed;

        uint64_t last_display_counter;
        bool ema_initialized;

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
