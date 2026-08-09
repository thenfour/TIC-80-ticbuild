// all audio capture logic details here.

#include "audio_capture.h"

#include <stdlib.h>
#include <string.h>

enum
{
    WavHeaderSize = 44,
    InitialCapacity = 64 * 1024,
    MaxCaptureBytes = 256 * 1024 * 1024,
};

struct tic_audio_capture
{
    tic_audio_capture_state state;
    uint8_t* wav;
    size_t size;
    size_t capacity;
    uint32_t sampleRate;
    uint16_t channels;
    char error[128];
};

static void setLe16(uint8_t* dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void setLe32(uint8_t* dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static void setError(tic_audio_capture* capture, const char* error)
{
    char message[sizeof capture->error];
    if(!error) error = "audio capture failed";
    strncpy(message, error, sizeof message - 1);
    message[sizeof message - 1] = '\0';

    free(capture->wav);
    capture->wav = NULL;
    capture->size = 0;
    capture->capacity = 0;
    capture->state = TIC_AUDIO_CAPTURE_ERROR;

    strncpy(capture->error, message, sizeof capture->error - 1);
    capture->error[sizeof capture->error - 1] = '\0';
}

static bool ensureCapacity(tic_audio_capture* capture, size_t required)
{
    if(required > MaxCaptureBytes)
    {
        setError(capture, "audio capture exceeded the 256 MiB limit");
        return false;
    }

    if(required <= capture->capacity)
        return true;

    size_t capacity = capture->capacity ? capture->capacity : InitialCapacity;
    while(capacity < required)
    {
        size_t next = capacity * 2;
        capacity = next > MaxCaptureBytes ? MaxCaptureBytes : next;
    }

    uint8_t* wav = realloc(capture->wav, capacity);
    if(!wav)
    {
        setError(capture, "not enough memory for audio capture");
        return false;
    }

    capture->wav = wav;
    capture->capacity = capacity;
    return true;
}

static void writeHeader(tic_audio_capture* capture)
{
    uint8_t* header = capture->wav;
    uint32_t dataSize = (uint32_t)(capture->size - WavHeaderSize);
    uint16_t bytesPerSample = sizeof(int16_t);
    uint16_t blockAlign = capture->channels * bytesPerSample;

    memcpy(header + 0, "RIFF", 4);
    setLe32(header + 4, dataSize + WavHeaderSize - 8);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    setLe32(header + 16, 16);
    setLe16(header + 20, 1);
    setLe16(header + 22, capture->channels);
    setLe32(header + 24, capture->sampleRate);
    setLe32(header + 28, capture->sampleRate * blockAlign);
    setLe16(header + 32, blockAlign);
    setLe16(header + 34, bytesPerSample * 8);
    memcpy(header + 36, "data", 4);
    setLe32(header + 40, dataSize);
}

tic_audio_capture* tic_audio_capture_create(void)
{
    tic_audio_capture* capture = calloc(1, sizeof *capture);
    if(capture)
        capture->state = TIC_AUDIO_CAPTURE_IDLE;
    return capture;
}

void tic_audio_capture_delete(tic_audio_capture* capture)
{
    if(capture)
    {
        free(capture->wav);
        free(capture);
    }
}

bool tic_audio_capture_start(tic_audio_capture* capture, uint32_t sampleRate, uint16_t channels)
{
    if(!capture || capture->state == TIC_AUDIO_CAPTURE_CAPTURING || capture->state == TIC_AUDIO_CAPTURE_STOPPING)
        return false;

    free(capture->wav);
    capture->wav = NULL;
    capture->size = 0;
    capture->capacity = 0;
    capture->error[0] = '\0';

    if(!sampleRate || !channels)
    {
        setError(capture, "invalid audio capture format");
        return false;
    }

    capture->sampleRate = sampleRate;
    capture->channels = channels;
    capture->state = TIC_AUDIO_CAPTURE_CAPTURING;

    if(!ensureCapacity(capture, WavHeaderSize))
        return false;

    memset(capture->wav, 0, WavHeaderSize);
    capture->size = WavHeaderSize;
    return true;
}

bool tic_audio_capture_end(tic_audio_capture* capture)
{
    if(!capture || capture->state != TIC_AUDIO_CAPTURE_CAPTURING)
        return false;

    capture->state = TIC_AUDIO_CAPTURE_STOPPING;
    return true;
}

bool tic_audio_capture_finish(
    tic_audio_capture* capture,
    tic_audio_capture_complete_callback onComplete,
    void* data)
{
    if(!capture || capture->state != TIC_AUDIO_CAPTURE_STOPPING)
        return false;

    writeHeader(capture);

    const char* error = onComplete ? onComplete(data, capture->wav, capture->size) : "audio capture has no output handler";
    if(error)
    {
        setError(capture, error);
        return false;
    }

    free(capture->wav);
    capture->wav = NULL;
    capture->size = 0;
    capture->capacity = 0;
    capture->state = TIC_AUDIO_CAPTURE_COMPLETE;
    return true;
}

bool tic_audio_capture_write(
    tic_audio_capture* capture,
    const int16_t* samples,
    size_t count,
    tic_audio_capture_complete_callback onComplete,
    void* data)
{
    if(!capture || (capture->state != TIC_AUDIO_CAPTURE_CAPTURING && capture->state != TIC_AUDIO_CAPTURE_STOPPING))
        return false;

    if(!samples && count)
    {
        setError(capture, "audio capture received invalid samples");
        return false;
    }

    if(count > (MaxCaptureBytes - capture->size) / sizeof *samples)
    {
        setError(capture, "audio capture exceeded the 256 MiB limit");
        return false;
    }

    size_t sampleBytes = count * sizeof *samples;
    if(!ensureCapacity(capture, capture->size + sampleBytes))
        return false;

    uint8_t* dst = capture->wav + capture->size;
    for(size_t i = 0; i < count; ++i)
        setLe16(dst + i * sizeof *samples, (uint16_t)samples[i]);

    capture->size += sampleBytes;

    if(capture->state == TIC_AUDIO_CAPTURE_STOPPING)
        return tic_audio_capture_finish(capture, onComplete, data);

    return true;
}

void tic_audio_capture_abort(tic_audio_capture* capture, const char* reason)
{
    if(capture && (capture->state == TIC_AUDIO_CAPTURE_CAPTURING || capture->state == TIC_AUDIO_CAPTURE_STOPPING))
        setError(capture, reason ? reason : "audio capture was interrupted");
}

tic_audio_capture_state tic_audio_capture_status(const tic_audio_capture* capture)
{
    return capture ? capture->state : TIC_AUDIO_CAPTURE_UNSUPPORTED;
}

const char* tic_audio_capture_error(const tic_audio_capture* capture)
{
    return capture && capture->state == TIC_AUDIO_CAPTURE_ERROR && capture->error[0] ? capture->error : NULL;
}

const char* tic_audio_capture_state_name(tic_audio_capture_state state)
{
    switch(state)
    {
    case TIC_AUDIO_CAPTURE_IDLE: return "idle";
    case TIC_AUDIO_CAPTURE_CAPTURING: return "capturing";
    case TIC_AUDIO_CAPTURE_STOPPING: return "stopping";
    case TIC_AUDIO_CAPTURE_COMPLETE: return "complete";
    case TIC_AUDIO_CAPTURE_ERROR: return "error";
    case TIC_AUDIO_CAPTURE_UNSUPPORTED:
    default: return "unsupported";
    }
}
