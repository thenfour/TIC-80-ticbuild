#include "ticbuild_remoting/fps.h"

#include <string.h>

static uint64_t tb_fps_compute_window_ticks(uint64_t freq)
{
    if(freq == 0) return 0;

    // window_ticks = freq * (NUM / DEN)
    // Avoid overflow by dividing if possible; freq is usually well within 64-bit.
    uint64_t window = (freq / TB_FPS_WINDOW_DEN) * TB_FPS_WINDOW_NUM;
    if(window == 0)
    {
        // Ensure the window isn't zero due to integer truncation.
        window = 1;
    }
    return window;
}

void tb_fps_init(tb_fps_tracker* t)
{
    if(!t) return;
    memset(t, 0, sizeof *t);
}

static void tb_fps_push_dt(tb_fps_tracker* t, uint64_t dt)
{
    if(t->count < TB_FPS_MAX_SAMPLES)
    {
        uint32_t idx = (t->head + t->count) % TB_FPS_MAX_SAMPLES;
        t->dt[idx] = dt;
        t->count++;
        t->sum_dt += dt;
        return;
    }

    // Overwrite oldest.
    t->sum_dt -= t->dt[t->head];
    t->dt[t->head] = dt;
    t->sum_dt += dt;
    t->head = (t->head + 1) % TB_FPS_MAX_SAMPLES;
}

static void tb_fps_trim_window(tb_fps_tracker* t)
{
    // Trim oldest frames until the summed time is within the window.
    while(t->count > 1 && t->sum_dt > t->window_ticks)
    {
        t->sum_dt -= t->dt[t->head];
        t->head = (t->head + 1) % TB_FPS_MAX_SAMPLES;
        t->count--;
    }
}

bool tb_fps_on_frame(tb_fps_tracker* t, uint64_t counter, uint64_t freq)
{
    if(!t) return false;
    if(freq == 0) return false;

    if(!t->initialized)
    {
        t->initialized = true;
        t->last_counter = counter;
        t->freq = freq;
        t->window_ticks = tb_fps_compute_window_ticks(freq);
        t->fps_rounded = 0;
        return false;
    }

    // If freq changes (well, that won't happen so this is untested), reset to avoid bogus math.
    if(t->freq != freq)
    {
        tb_fps_init(t);
        t->initialized = true;
        t->last_counter = counter;
        t->freq = freq;
        t->window_ticks = tb_fps_compute_window_ticks(freq);
        return false;
    }

    uint64_t dt = counter - t->last_counter;
    t->last_counter = counter;

    if(dt == 0) return false;

    tb_fps_push_dt(t, dt);
    tb_fps_trim_window(t);

    if(t->sum_dt == 0 || t->count == 0) return false;

    // Rounded FPS = round((count * freq) / sum_dt)
    uint64_t num = (uint64_t)t->count * t->freq;
    int fps = (int)((num + (t->sum_dt / 2)) / t->sum_dt);

    if(fps != t->fps_rounded)
    {
        t->fps_rounded = fps;
        return true;
    }

    return false;
}

int tb_fps_get(const tb_fps_tracker* t)
{
    return t ? t->fps_rounded : 0;
}
