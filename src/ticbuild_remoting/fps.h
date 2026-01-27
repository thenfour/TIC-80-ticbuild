#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// rolling-window FPS calc

#ifndef TB_FPS_MAX_SAMPLES
#    define TB_FPS_MAX_SAMPLES 256
#endif

// Window length in seconds as a rational
#ifndef TB_FPS_WINDOW_NUM
#    define TB_FPS_WINDOW_NUM 1
#endif
#ifndef TB_FPS_WINDOW_DEN
#    define TB_FPS_WINDOW_DEN 2
#endif

    typedef struct
    {
        uint64_t last_counter;
        uint64_t freq;
        uint64_t window_ticks;

        // ring buffer of frame durations (ticks)
        uint64_t dt[TB_FPS_MAX_SAMPLES];
        uint32_t head;
        uint32_t count;
        uint64_t sum_dt;

        int fps_rounded;
        bool initialized;
    } tb_fps_tracker;

    void tb_fps_init(tb_fps_tracker* t);

    // Call once per rendered frame with a monotonic counter/freq.
    // Returns true if the rounded FPS value changed.
    bool tb_fps_on_frame(tb_fps_tracker* t, uint64_t counter, uint64_t freq);

    int tb_fps_get(const tb_fps_tracker* t);

#ifdef __cplusplus
}
#endif
