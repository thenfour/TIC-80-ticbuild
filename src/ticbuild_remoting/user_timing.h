#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "api.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Installs wrappers around the currently active user callbacks (TIC/SCN/BDR)
    // so we can measure host time spent in them.
    // call every frame for cart reload safety
    void ticbuild_user_timing_install(tic_mem* tic);

    // Finalizes the current frame's timing into a snapshot.
    // Call once after the frame was rendered
    void ticbuild_user_timing_end_frame(tic_mem* tic);

    // returns false if unavailable.
    bool ticbuild_user_timing_get_last_ms10(
        tic_mem* tic,
        uint32_t* tic_ms10,
        uint32_t* scn_ms10,
        uint32_t* bdr_ms10,
        uint32_t* total_ms10);

    // returns false if unavailable.
    bool ticbuild_user_timing_get_last_cycles(
        tic_mem* tic,
        uint64_t* tic_cycles,
        uint64_t* scn_cycles,
        uint64_t* bdr_cycles);

#ifdef __cplusplus
}
#endif
