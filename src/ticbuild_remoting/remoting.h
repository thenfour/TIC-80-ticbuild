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

#ifdef __cplusplus
}
#endif
