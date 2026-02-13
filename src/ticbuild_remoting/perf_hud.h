#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "api.h"
#include "ticbuild_remoting/fps.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void ticbuild_perf_hud_draw(
        tic_mem* tic,
        tb_fps_tracker* fps,
        uint64_t counter,
        uint64_t freq,
        bool enabled,
        bool run_mode);

#ifdef __cplusplus
}
#endif
