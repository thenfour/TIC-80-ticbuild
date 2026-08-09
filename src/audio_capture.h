// all audio capture logic details here.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    TIC_AUDIO_CAPTURE_UNSUPPORTED = -1,
    TIC_AUDIO_CAPTURE_IDLE,
    TIC_AUDIO_CAPTURE_CAPTURING,
    TIC_AUDIO_CAPTURE_STOPPING,
    TIC_AUDIO_CAPTURE_COMPLETE,
    TIC_AUDIO_CAPTURE_ERROR,
} tic_audio_capture_state;

typedef struct tic_audio_capture tic_audio_capture;

typedef const char* (*tic_audio_capture_complete_callback)(
    void* data,
    const uint8_t* wav,
    size_t size);

tic_audio_capture* tic_audio_capture_create(void);
void tic_audio_capture_delete(tic_audio_capture* capture);

bool tic_audio_capture_start(tic_audio_capture* capture, uint32_t sampleRate, uint16_t channels);
bool tic_audio_capture_end(tic_audio_capture* capture);
bool tic_audio_capture_write(
    tic_audio_capture* capture,
    const int16_t* samples,
    size_t count,
    tic_audio_capture_complete_callback onComplete,
    void* data);
bool tic_audio_capture_finish(
    tic_audio_capture* capture,
    tic_audio_capture_complete_callback onComplete,
    void* data);
void tic_audio_capture_abort(tic_audio_capture* capture, const char* reason);

tic_audio_capture_state tic_audio_capture_status(const tic_audio_capture* capture);
const char* tic_audio_capture_error(const tic_audio_capture* capture);
const char* tic_audio_capture_state_name(tic_audio_capture_state state);

