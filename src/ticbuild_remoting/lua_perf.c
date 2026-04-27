#include "ticbuild_remoting/lua_perf.h"

#include "core/core.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) || defined(__TIC_WINDOWS__)
#include <windows.h>
#endif

enum { TB_LUA_HOOK_STEP = 1000 };
static const double TB_LUA_PERF_ALPHA_DEFAULT = 0.25;
static const double TB_LUA_PERF_ALPHA_MIN = 0.0;
static const double TB_LUA_PERF_ALPHA_MAX = 1.0;
enum { TB_LUA_PROFILER_STACK_MAX = 64 };
enum { TB_LUA_PROFILER_STACK_TEXT_MAX = 4096 };

static void tb_lua_hook(lua_State* L, lua_Debug* ar);

typedef struct
{
    bool active;
    tb_lua_profiler_mode mode;
    uint32_t instruction_interval;
    uint32_t wall_clock_period_micros;
    uint64_t start_counter;
    uint64_t instruction_accum;
    uint64_t next_due_tick;
    char** samples;
    size_t sample_count;
    size_t sample_cap;
} tb_lua_profiler_session;

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

    tb_lua_perf_user_slot user_slots[TB_LUA_PERF_USER_SLOT_COUNT];
    tb_lua_profiler_session profiler;

    bool hook_installed;
} tb_lua_perf_slot;

// keep small; multiple cores are uncommon
enum { SLOT_COUNT = 4 };

static tb_lua_perf_slot Slots[SLOT_COUNT];

static void tb_set_err(char* err, size_t errcap, const char* msg)
{
    if(!err || errcap == 0) return;
    if(!msg) msg = "error";
    strncpy(err, msg, errcap - 1);
    err[errcap - 1] = '\0';
}

static double tb_clamp_alpha(double alpha)
{
    if(alpha < TB_LUA_PERF_ALPHA_MIN) return TB_LUA_PERF_ALPHA_MIN;
    if(alpha > TB_LUA_PERF_ALPHA_MAX) return TB_LUA_PERF_ALPHA_MAX;
    return alpha;
}

static void tb_init_user_slots(tb_lua_perf_slot* slot)
{
    if(!slot) return;

    for(int i = 0; i < TB_LUA_PERF_USER_SLOT_COUNT; i++)
    {
        slot->user_slots[i].active = false;
        slot->user_slots[i].value = 0.0;
        slot->user_slots[i].smoothing_alpha = TB_LUA_PERF_ALPHA_DEFAULT;
        snprintf(slot->user_slots[i].label, sizeof slot->user_slots[i].label, "USR%d", i + 1);
    }
}

static void tb_set_default_user_slot(tb_lua_perf_user_slot* slot, int index)
{
    if(!slot) return;
    slot->active = false;
    slot->value = 0.0;
    slot->smoothing_alpha = TB_LUA_PERF_ALPHA_DEFAULT;
    snprintf(slot->label, sizeof slot->label, "USR%d", index + 1);
}

static void tb_profiler_clear_samples(tb_lua_profiler_session* profiler)
{
    if(!profiler) return;

    for(size_t i = 0; i < profiler->sample_count; i++)
        free(profiler->samples[i]);

    free(profiler->samples);
    profiler->samples = NULL;
    profiler->sample_count = 0;
    profiler->sample_cap = 0;
}

static void tb_profiler_reset(tb_lua_profiler_session* profiler)
{
    if(!profiler) return;

    tb_profiler_clear_samples(profiler);
    profiler->active = false;
    profiler->mode = TB_LUA_PROFILER_MODE_OFF;
    profiler->instruction_interval = 0;
    profiler->wall_clock_period_micros = 0;
    profiler->start_counter = 0;
    profiler->instruction_accum = 0;
    profiler->next_due_tick = 0;
}

static tb_lua_profiler_mode tb_parse_profiler_mode(const char* mode)
{
    if(!mode || !mode[0]) return TB_LUA_PROFILER_MODE_OFF;
    if(strcmp(mode, "instructions") == 0) return TB_LUA_PROFILER_MODE_INSTRUCTIONS;
    if(strcmp(mode, "wallclock") == 0) return TB_LUA_PROFILER_MODE_WALLCLOCK;
    return TB_LUA_PROFILER_MODE_OFF;
}

static int tb_internal_hook_step(const tb_lua_perf_slot* slot)
{
    int step = TB_LUA_HOOK_STEP;

    if(slot && slot->profiler.active && slot->profiler.instruction_interval > 0 && (int)slot->profiler.instruction_interval < step)
        step = (int)slot->profiler.instruction_interval;

    if(step < 1)
        step = 1;

    return step;
}

static void tb_refresh_hook(tb_lua_perf_slot* slot)
{
    if(!slot || !slot->lua)
        return;

    bool capture_prev = !slot->hook_installed || lua_gethook(slot->lua) != tb_lua_hook;
    if(capture_prev)
    {
        slot->prev_hook = lua_gethook(slot->lua);
        slot->prev_mask = lua_gethookmask(slot->lua);
        slot->prev_count = lua_gethookcount(slot->lua);
        slot->prev_count_accum = 0;
    }

    int hook_step = tb_internal_hook_step(slot);
    if((slot->prev_mask & LUA_MASKCOUNT) && slot->prev_count > 0 && slot->prev_count < hook_step)
        hook_step = slot->prev_count;

    slot->hook_step = hook_step;
    lua_sethook(slot->lua, tb_lua_hook, slot->prev_mask | LUA_MASKCOUNT, hook_step);
    slot->hook_installed = true;
}

static void tb_sanitize_label(char* s)
{
    if(!s) return;

    for(char* p = s; *p; p++)
    {
        if(*p == ';' || *p == '\r' || *p == '\n' || *p == '\t')
            *p = ':';
    }
}

// TIC-80's lua env doesn't have a concept of filenames so it synthesizes
// one. they appear like:
// [string "-- title:   game title..."]:23
// this strips that down to "main" so there's still a filename but it's more sane to look at
// and doesn't pollute the profile output.
static const char* tb_profiler_normalize_src(const lua_Debug* ar)
{
    if(!ar)
        return "?";

    if(ar->short_src[0] == '\0')
        return "?";

    if(strncmp(ar->short_src, "[string \"", 9) == 0)
        return "main";

    return ar->short_src;
}

static bool tb_profiler_append_sample(tb_lua_perf_slot* slot, const char* stack)
{
    if(!slot || !stack || !stack[0]) return false;

    tb_lua_profiler_session* profiler = &slot->profiler;
    if(profiler->sample_count >= profiler->sample_cap)
    {
        size_t next_cap = profiler->sample_cap ? profiler->sample_cap * 2 : 256;
        char** next = (char**)realloc(profiler->samples, next_cap * sizeof(*next));
        if(!next)
            return false;

        profiler->samples = next;
        profiler->sample_cap = next_cap;
    }

    size_t len = strlen(stack);
    char* copy = (char*)malloc(len + 1);
    if(!copy)
        return false;

    memcpy(copy, stack, len + 1);
    profiler->samples[profiler->sample_count++] = copy;
    return true;
}

static void tb_profiler_collect_sample(tb_lua_perf_slot* slot)
{
    if(!slot || !slot->lua || !slot->profiler.active)
        return;

    char frames[TB_LUA_PROFILER_STACK_MAX][256];
    int frame_count = 0;

    for(int level = 0; level < TB_LUA_PROFILER_STACK_MAX; level++)
    {
        lua_Debug ar;
        memset(&ar, 0, sizeof ar);
        if(!lua_getstack(slot->lua, level, &ar))
            break;

        if(!lua_getinfo(slot->lua, "nSl", &ar))
            break;

        const char* src = tb_profiler_normalize_src(&ar);
        int line = ar.currentline > 0 ? ar.currentline : ar.linedefined;

        if(ar.name && ar.name[0])
            snprintf(frames[frame_count], sizeof frames[frame_count], "%s@%s:%d", ar.name, src, line);
        else
            snprintf(frames[frame_count], sizeof frames[frame_count], "%s:%d", src, line);

        tb_sanitize_label(frames[frame_count]);
        frame_count++;
    }

    char stack_text[TB_LUA_PROFILER_STACK_TEXT_MAX];
    size_t pos = 0;
    stack_text[0] = '\0';

    if(frame_count == 0)
    {
        strncpy(stack_text, "main", sizeof stack_text - 1);
        stack_text[sizeof stack_text - 1] = '\0';
        tb_profiler_append_sample(slot, stack_text);
        return;
    }

    for(int i = frame_count - 1; i >= 0; i--)
    {
        size_t len = strlen(frames[i]);
        if(pos + len + (pos ? 1 : 0) + 1 >= sizeof stack_text)
            break;

        if(pos)
            stack_text[pos++] = ';';

        memcpy(stack_text + pos, frames[i], len);
        pos += len;
    }

    stack_text[pos] = '\0';
    if(pos > 0)
        tb_profiler_append_sample(slot, stack_text);
}

static bool tb_profiler_get_counter_info(tb_lua_perf_slot* slot, uint64_t* out_counter, uint64_t* out_freq)
{
    if(!slot || !slot->key) return false;

    tic_core* core = (tic_core*)slot->key;
    if(!core->data || !core->data->counter || !core->data->freq)
        return false;

    uint64_t counter = core->data->counter(core->data->data);
    uint64_t freq = core->data->freq(core->data->data);
    if(freq == 0)
        return false;

    if(out_counter) *out_counter = counter;
    if(out_freq) *out_freq = freq;
    return true;
}

static void tb_profiler_on_count(tb_lua_perf_slot* slot)
{
    if(!slot || !slot->profiler.active)
        return;

    tb_lua_profiler_session* profiler = &slot->profiler;

    if(profiler->mode == TB_LUA_PROFILER_MODE_INSTRUCTIONS)
    {
        profiler->instruction_accum += (uint64_t)slot->hook_step;
        while(profiler->instruction_accum >= profiler->instruction_interval)
        {
            profiler->instruction_accum -= profiler->instruction_interval;
            tb_profiler_collect_sample(slot);
        }
        return;
    }

    if(profiler->mode == TB_LUA_PROFILER_MODE_WALLCLOCK)
    {
        uint64_t counter = 0;
        uint64_t freq = 0;
        if(!tb_profiler_get_counter_info(slot, &counter, &freq))
            return;

        uint64_t period_ticks = ((uint64_t)profiler->wall_clock_period_micros * freq + 500000ULL) / 1000000ULL;
        if(period_ticks == 0)
            period_ticks = 1;

        if(profiler->next_due_tick == 0)
        {
            profiler->next_due_tick = counter + period_ticks;
            return;
        }

        if(counter >= profiler->next_due_tick)
        {
            tb_profiler_collect_sample(slot);
            profiler->next_due_tick = counter + period_ticks;
        }
    }
}

static int tb_compare_samples(const void* a, const void* b)
{
    const char* const* sa = (const char* const*)a;
    const char* const* sb = (const char* const*)b;
    return strcmp(*sa, *sb);
}

static bool tb_profiler_default_output_path(char* out, size_t cap, char* err, size_t errcap)
{
    if(!out || cap == 0)
    {
        tb_set_err(err, errcap, "missing output buffer");
        return false;
    }

#if defined(_WIN32) || defined(__TIC_WINDOWS__)
    char temp[MAX_PATH];
    DWORD len = GetTempPathA((DWORD)sizeof temp, temp);
    if(len == 0 || len >= sizeof temp)
    {
        tb_set_err(err, errcap, "failed to resolve temp path");
        return false;
    }

    int written = snprintf(out, cap, "%stic80-lua-profiler-%lu.txt", temp, (unsigned long)GetCurrentProcessId());
#else
    int written = snprintf(out, cap, "tic80-lua-profiler-%llu.txt", (unsigned long long)time(NULL));
#endif

    if(written < 0 || (size_t)written >= cap)
    {
        tb_set_err(err, errcap, "output path too long");
        return false;
    }

    return true;
}

static bool tb_profiler_write_output(tb_lua_perf_slot* slot, const char* output_path, char* saved_path, size_t saved_path_cap, char* err, size_t errcap)
{
    if(!slot)
    {
        tb_set_err(err, errcap, "profiler unavailable");
        return false;
    }

    char path_buf[512];
    const char* path = output_path;
    if(!path || !path[0])
    {
        if(!tb_profiler_default_output_path(path_buf, sizeof path_buf, err, errcap))
            return false;
        path = path_buf;
    }

    FILE* file = fopen(path, "wb");
    if(!file)
    {
        tb_set_err(err, errcap, "failed to open profiler output");
        return false;
    }

    tb_lua_profiler_session* profiler = &slot->profiler;
    if(profiler->sample_count > 1)
        qsort(profiler->samples, profiler->sample_count, sizeof(*profiler->samples), tb_compare_samples);

    for(size_t i = 0; i < profiler->sample_count; )
    {
        size_t count = 1;
        while(i + count < profiler->sample_count && strcmp(profiler->samples[i], profiler->samples[i + count]) == 0)
            count++;

        fprintf(file, "%s %zu\n", profiler->samples[i], count);
        i += count;
    }

    fclose(file);

    if(saved_path && saved_path_cap > 0)
    {
        strncpy(saved_path, path, saved_path_cap - 1);
        saved_path[saved_path_cap - 1] = '\0';
    }

    return true;
}

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
            tb_init_user_slots(&Slots[i]);
            tb_profiler_reset(&Slots[i].profiler);
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
        tb_profiler_on_count(slot);

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

    bool vm_changed = (slot->lua != lua);

    slot->lua = lua;
    slot->instr_counter = 0;

    if(vm_changed)
        tb_init_user_slots(slot);
    if(vm_changed)
        slot->profiler.next_due_tick = 0;

    tb_refresh_hook(slot);
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

bool ticbuild_lua_perf_set_user_slot(
    tic_mem* tic,
    int slot,
    double value,
    const char* label,
    bool set_label,
    double smoothing_alpha,
    bool set_alpha)
{
    if(slot < 0 || slot >= TB_LUA_PERF_USER_SLOT_COUNT)
        return false;

    tb_lua_perf_slot* s = get_slot(tic, true);
    if(!s) return false;

    tb_lua_perf_user_slot* dst = &s->user_slots[slot];
    dst->active = true;
    dst->value = value;

    if(set_label && label)
    {
        strncpy(dst->label, label, sizeof dst->label - 1);
        dst->label[sizeof dst->label - 1] = '\0';
    }

    if(set_alpha)
        dst->smoothing_alpha = tb_clamp_alpha(smoothing_alpha);

    return true;
}

bool ticbuild_lua_perf_clear_user_slot(tic_mem* tic, int slot)
{
    if(slot < 0 || slot >= TB_LUA_PERF_USER_SLOT_COUNT)
        return false;

    tb_lua_perf_slot* s = get_slot(tic, true);
    if(!s) return false;

    tb_set_default_user_slot(&s->user_slots[slot], slot);
    return true;
}

void ticbuild_lua_perf_get_user_slots(tic_mem* tic, tb_lua_perf_user_slot out_slots[TB_LUA_PERF_USER_SLOT_COUNT])
{
    if(!out_slots) return;

    tb_lua_perf_slot* s = get_slot(tic, false);
    if(!s)
    {
        for(int i = 0; i < TB_LUA_PERF_USER_SLOT_COUNT; i++)
            tb_set_default_user_slot(&out_slots[i], i);
        return;
    }

    for(int i = 0; i < TB_LUA_PERF_USER_SLOT_COUNT; i++)
        out_slots[i] = s->user_slots[i];
}

bool ticbuild_lua_profiler_start(
    tic_mem* tic,
    const char* mode,
    uint32_t instruction_interval,
    uint32_t wall_clock_period_micros,
    char* err,
    size_t errcap)
{
    tb_lua_profiler_mode parsed_mode = tb_parse_profiler_mode(mode);
    if(parsed_mode == TB_LUA_PROFILER_MODE_OFF)
    {
        tb_set_err(err, errcap, "invalid profiler mode");
        return false;
    }

    if(instruction_interval == 0)
    {
        tb_set_err(err, errcap, "instruction interval must be > 0");
        return false;
    }

    if(parsed_mode == TB_LUA_PROFILER_MODE_WALLCLOCK && wall_clock_period_micros == 0)
    {
        tb_set_err(err, errcap, "wall clock period must be > 0");
        return false;
    }

    ticbuild_lua_perf_install(tic);

    tb_lua_perf_slot* slot = get_slot(tic, false);
    if(!slot || !slot->lua)
    {
        tb_set_err(err, errcap, "lua profiler not available");
        return false;
    }

    if(slot->profiler.active)
    {
        tb_set_err(err, errcap, "lua profiler already running");
        return false;
    }

    tb_profiler_clear_samples(&slot->profiler);
    slot->profiler.active = true;
    slot->profiler.mode = parsed_mode;
    slot->profiler.instruction_interval = instruction_interval;
    slot->profiler.wall_clock_period_micros = wall_clock_period_micros;
    slot->profiler.start_counter = 0;
    slot->profiler.instruction_accum = 0;
    slot->profiler.next_due_tick = 0;

    {
        uint64_t counter = 0;
        if(tb_profiler_get_counter_info(slot, &counter, NULL))
            slot->profiler.start_counter = counter;
    }

    tb_refresh_hook(slot);
    return true;
}

bool ticbuild_lua_profiler_stop(
    tic_mem* tic,
    const char* output_path,
    char* saved_path,
    size_t saved_path_cap,
    char* err,
    size_t errcap)
{
    tb_lua_perf_slot* slot = get_slot(tic, false);
    if(!slot || !slot->profiler.active)
    {
        tb_set_err(err, errcap, "lua profiler not running");
        return false;
    }

    bool ok = tb_profiler_write_output(slot, output_path, saved_path, saved_path_cap, err, errcap);

    tb_profiler_reset(&slot->profiler);
    if(slot->lua)
        tb_refresh_hook(slot);

    return ok;
}

bool ticbuild_lua_profiler_get_status(tic_mem* tic, tb_lua_profiler_status* out_status)
{
    if(!out_status)
        return false;

    memset(out_status, 0, sizeof *out_status);

    tb_lua_perf_slot* slot = get_slot(tic, false);
    if(!slot)
        return true;

    out_status->running = slot->profiler.active;
    out_status->mode = slot->profiler.mode;
    out_status->instruction_interval = slot->profiler.instruction_interval;
    out_status->wall_clock_period_micros = slot->profiler.wall_clock_period_micros;
    if(slot->profiler.active && slot->profiler.start_counter != 0)
    {
        uint64_t counter = 0;
        uint64_t freq = 0;
        if(tb_profiler_get_counter_info(slot, &counter, &freq) && freq > 0 && counter >= slot->profiler.start_counter)
            out_status->elapsed_seconds = (uint32_t)((counter - slot->profiler.start_counter) / freq);
    }
    return true;
}
