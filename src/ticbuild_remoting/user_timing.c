#include "ticbuild_remoting/user_timing.h"

#include "core/core.h"

#include <string.h>

typedef struct
{
    tic_mem* key; // use as a key, don't actually use the ptr.

    // original hooks so we can forward / restore
    tic_tick orig_tick;
    tic_scanline orig_scanline;
    tic_border orig_border;

    u64 freq;
    u64 cur_tic_ticks;
    u64 cur_scn_ticks;
    u64 cur_bdr_ticks;

    u64 last_tic_ticks;
    u64 last_scn_ticks;
    u64 last_bdr_ticks;
    u64 last_total_ticks;
} tb_user_timing_slot;

// we don't want to add a bunch of code to core.c, so we do our own bookkeeping.
// this way we can "attach" ourselves to a core instance, without actually modifying it.
// a slot = one tic_mem instance.
enum { SLOT_COUNT = 4 };

static tb_user_timing_slot Slots[SLOT_COUNT];

static tb_user_timing_slot* get_slot(tic_mem* tic, bool create)
{
    if(!tic) return NULL;

    for(size_t i = 0; i < COUNT_OF(Slots); ++i) {
        if(Slots[i].key == tic) return &Slots[i];
    }

    if(!create) return NULL;

    for(size_t i = 0; i < COUNT_OF(Slots); ++i)
    {
        if(Slots[i].key == NULL)
        {
            memset(&Slots[i], 0, sizeof Slots[i]);
            Slots[i].key = tic;
            return &Slots[i];
        }
    }

    return NULL;
}

static inline uint32_t ticks_to_ms10(u64 ticks, u64 freq)
{
    if(freq == 0) return 0;
    // ms10 = round(ticks * 10000 / freq)
    return (uint32_t)((ticks * 10000ULL + (freq / 2ULL)) / freq);
}

static void timing_tick_wrapper(tic_mem* memory)
{
    tic_core* core = (tic_core*)memory;
    tb_user_timing_slot* slot = get_slot(memory, false);

    if(!slot || !slot->orig_tick)
    {
        // Shouldn't happen, but avoid crashing if install ordering gets weird.
        if(core && core->state.tick && core->state.tick != timing_tick_wrapper)
            core->state.tick(memory);
        return;
    }

    // Reset current frame accumulators at the start of TIC.
    slot->cur_tic_ticks = 0;
    slot->cur_scn_ticks = 0;
    slot->cur_bdr_ticks = 0;

    if(core && core->data && core->data->freq)
        slot->freq = core->data->freq(core->data->data);

    if(core && core->data && core->data->counter)
    {
        u64 t0 = core->data->counter(core->data->data);
        slot->orig_tick(memory);
        u64 t1 = core->data->counter(core->data->data);
        slot->cur_tic_ticks += (t1 - t0);
    }
    else
        slot->orig_tick(memory);
}

static void timing_scanline_wrapper(tic_mem* memory, s32 row, void* data)
{
    tic_core* core = (tic_core*)memory;
    tb_user_timing_slot* slot = get_slot(memory, false);

    if(!slot || !slot->orig_scanline)
        return;

    if(core && core->data && core->data->counter)
    {
        u64 t0 = core->data->counter(core->data->data);
        slot->orig_scanline(memory, row, data);
        u64 t1 = core->data->counter(core->data->data);
        slot->cur_scn_ticks += (t1 - t0);
    }
    else
        slot->orig_scanline(memory, row, data);
}

static void timing_border_wrapper(tic_mem* memory, s32 row, void* data)
{
    tic_core* core = (tic_core*)memory;
    tb_user_timing_slot* slot = get_slot(memory, false);

    if(!slot || !slot->orig_border)
        return;

    if(core && core->data && core->data->counter)
    {
        u64 t0 = core->data->counter(core->data->data);
        slot->orig_border(memory, row, data);
        u64 t1 = core->data->counter(core->data->data);
        slot->cur_bdr_ticks += (t1 - t0);
    }
    else
        slot->orig_border(memory, row, data);
}

void ticbuild_user_timing_install(tic_mem* tic)
{
    if(!tic) return;

    tic_core* core = (tic_core*)tic;
    if(!core->state.initialized)
        return;

    tb_user_timing_slot* slot = get_slot(tic, true);
    if(!slot) return;

    // Wrap TIC.
    if(core->state.tick && core->state.tick != timing_tick_wrapper)
    {
        slot->orig_tick = core->state.tick;
        core->state.tick = timing_tick_wrapper;
    }

    // Wrap SCN.
    if(core->state.callback.scanline && core->state.callback.scanline != timing_scanline_wrapper)
    {
        slot->orig_scanline = core->state.callback.scanline;
        core->state.callback.scanline = timing_scanline_wrapper;
    }

    // Wrap BDR.
    if(core->state.callback.border && core->state.callback.border != timing_border_wrapper)
    {
        slot->orig_border = core->state.callback.border;
        core->state.callback.border = timing_border_wrapper;
    }
}

void ticbuild_user_timing_end_frame(tic_mem* tic)
{
    tb_user_timing_slot* slot = get_slot(tic, false);
    if(!slot) return;

    slot->last_tic_ticks = slot->cur_tic_ticks;
    slot->last_scn_ticks = slot->cur_scn_ticks;
    slot->last_bdr_ticks = slot->cur_bdr_ticks;
    slot->last_total_ticks = slot->last_tic_ticks + slot->last_scn_ticks + slot->last_bdr_ticks;
}

bool ticbuild_user_timing_get_last_ms10(
    tic_mem* tic,
    uint32_t* tic_ms10,
    uint32_t* scn_ms10,
    uint32_t* bdr_ms10,
    uint32_t* total_ms10)
{
    if(tic_ms10) *tic_ms10 = 0;
    if(scn_ms10) *scn_ms10 = 0;
    if(bdr_ms10) *bdr_ms10 = 0;
    if(total_ms10) *total_ms10 = 0;

    tb_user_timing_slot* slot = get_slot(tic, false);
    if(!slot || slot->freq == 0)
        return false;

    if(tic_ms10) *tic_ms10 = ticks_to_ms10(slot->last_tic_ticks, slot->freq);
    if(scn_ms10) *scn_ms10 = ticks_to_ms10(slot->last_scn_ticks, slot->freq);
    if(bdr_ms10) *bdr_ms10 = ticks_to_ms10(slot->last_bdr_ticks, slot->freq);
    if(total_ms10) *total_ms10 = ticks_to_ms10(slot->last_total_ticks, slot->freq);

    return true;
}
