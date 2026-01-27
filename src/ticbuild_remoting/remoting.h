#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TicbuildRemoting TicbuildRemoting;

typedef struct
{
    void* userdata;

    void (*hello)(void* userdata, char* out, size_t outcap);

    bool (*load)(void* userdata, const char* cart_path, bool run, char* err, size_t errcap);
    bool (*restart)(void* userdata, char* err, size_t errcap);
    bool (*quit)(void* userdata, char* err, size_t errcap);

    bool (*sync)(void* userdata, uint32_t flags, char* err, size_t errcap);
    bool (*poke)(void* userdata, uint32_t addr, const uint8_t* data, size_t size, char* err, size_t errcap);
    bool (*peek)(void* userdata, uint32_t addr, uint32_t size, uint8_t* out, char* err, size_t errcap);

    bool (*eval)(void* userdata, const char* code, char* err, size_t errcap);
} ticbuild_remoting_callbacks;

TicbuildRemoting* ticbuild_remoting_create(int port, const ticbuild_remoting_callbacks* callbacks);
void ticbuild_remoting_close(TicbuildRemoting* ctx);

void ticbuild_remoting_tick(TicbuildRemoting* ctx);

// Per-frame timing hook (call once per rendered frame).
// `counter`/`freq` should come from tic_sys_counter_get()/tic_sys_freq_get().
void ticbuild_remoting_on_frame(TicbuildRemoting* ctx, uint64_t counter, uint64_t freq);

// Current FPS (or 0)
int ticbuild_remoting_get_fps(const TicbuildRemoting* ctx);

// Builds a short status string suitable for the window title, e.g.
// `FPS: 60 | listening on 127.0.0.1:9977`.
void ticbuild_remoting_get_title_info(const TicbuildRemoting* ctx, char* out, size_t outcap);

// Returns whether the title should be refreshed (and clears the internal dirty flag).
bool ticbuild_remoting_take_title_dirty(TicbuildRemoting* ctx);

#ifdef __cplusplus
}
#endif
