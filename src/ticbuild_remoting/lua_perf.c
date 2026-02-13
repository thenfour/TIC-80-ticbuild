#include "ticbuild_remoting/lua_perf.h"

#include "core/core.h"

#include <lua.h>
#include <string.h>

enum { TB_LUA_HOOK_STEP = 250 };

typedef struct
{
    tic_mem* key; // use as a key, don't actually use the ptr.
    lua_State* lua;

    lua_Hook prev_hook;
    int prev_mask;
    int prev_count;
    int hook_step;

    uint64_t instr_counter;
    uint64_t prev_count_accum;

    bool hook_installed;
} tb_lua_perf_slot;

// keep small; multiple cores are uncommon
enum { SLOT_COUNT = 4 };

static tb_lua_perf_slot Slots[SLOT_COUNT];

static tb_lua_perf_slot* get_slot(tic_mem* tic, bool create)
{
    if(!tic) return NULL;

    for(size_t i = 0; i < COUNT_OF(Slots); ++i)
    {
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

static tb_lua_perf_slot* get_slot_by_lua(lua_State* lua)
{
    if(!lua) return NULL;

    for(size_t i = 0; i < COUNT_OF(Slots); ++i)
    {
        if(Slots[i].lua == lua) return &Slots[i];
    }

    return NULL;
}

static int event_to_mask(int event)
{
    switch(event)
    {
        case LUA_HOOKCALL: return LUA_MASKCALL;
        case LUA_HOOKRET: return LUA_MASKRET;
        case LUA_HOOKLINE: return LUA_MASKLINE;
        case LUA_HOOKCOUNT: return LUA_MASKCOUNT;
        case LUA_HOOKTAILCALL: return LUA_MASKCALL;
        default: return 0;
    }
}

static void tb_lua_hook(lua_State* L, lua_Debug* ar)
{
    tb_lua_perf_slot* slot = get_slot_by_lua(L);
    if(!slot) return;

    if(ar->event == LUA_HOOKCOUNT)
    {
        slot->instr_counter += (uint64_t)slot->hook_step;

        if(slot->prev_hook && slot->prev_hook != tb_lua_hook)
        {
            if((slot->prev_mask & LUA_MASKCOUNT) && slot->prev_count > 0)
            {
                slot->prev_count_accum += (uint64_t)slot->hook_step;
                if(slot->prev_count_accum >= (uint64_t)slot->prev_count)
                {
                    slot->prev_count_accum = 0;
                    slot->prev_hook(L, ar);
                }
            }
        }
        return;
    }

    if(slot->prev_hook && slot->prev_hook != tb_lua_hook)
    {
        int mask = event_to_mask(ar->event);
        if(mask && (slot->prev_mask & mask))
            slot->prev_hook(L, ar);
    }
}

void ticbuild_lua_perf_install(tic_mem* tic)
{
    if(!tic) return;

    tic_core* core = (tic_core*)tic;
    lua_State* lua = core->currentVM;
    if(!lua) return;

    tb_lua_perf_slot* slot = get_slot(tic, true);
    if(!slot) return;

    if(slot->lua == lua && slot->hook_installed)
    {
        if(lua_gethook(lua) == tb_lua_hook)
            return;
    }

    slot->lua = lua;
    slot->instr_counter = 0;
    slot->prev_count_accum = 0;

    slot->prev_hook = lua_gethook(lua);
    slot->prev_mask = lua_gethookmask(lua);
    slot->prev_count = lua_gethookcount(lua);

    int hook_step = TB_LUA_HOOK_STEP;
    if((slot->prev_mask & LUA_MASKCOUNT) && slot->prev_count > 0 && slot->prev_count < hook_step)
        hook_step = slot->prev_count;
    slot->hook_step = hook_step;

    int new_mask = slot->prev_mask | LUA_MASKCOUNT;
    lua_sethook(lua, tb_lua_hook, new_mask, hook_step);

    slot->hook_installed = true;
}

void ticbuild_lua_perf_reset_counter(tic_mem* tic)
{
    tb_lua_perf_slot* slot = get_slot(tic, false);
    if(!slot) return;

    slot->instr_counter = 0;
    slot->prev_count_accum = 0;
}

uint64_t ticbuild_lua_perf_get_counter(tic_mem* tic)
{
    tb_lua_perf_slot* slot = get_slot(tic, false);
    if(!slot) return 0;

    return slot->instr_counter;
}

uint64_t ticbuild_lua_perf_get_mem_bytes(tic_mem* tic)
{
    if(!tic) return 0;

    tic_core* core = (tic_core*)tic;
    lua_State* lua = core->currentVM;
    if(!lua) return 0;

    int kb = lua_gc(lua, LUA_GCCOUNT, 0);
    int kb8 = lua_gc(lua, LUA_GCCOUNTB, 0);
    return (uint64_t)kb * 1024ULL + (uint64_t)kb8;
}
