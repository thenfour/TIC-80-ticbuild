#include "api.h"
#include "ticbuild_remoting/binary_literal.h"
#include "ticbuild_remoting/script_error_payload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static tic_script_error CapturedError;
static bool ErrorCaptured;
static u64 Counter;

static void captureScriptError(void* data, const tic_script_error* error)
{
    (void)data;
    CapturedError = *error;
    ErrorCaptured = true;
}

static void ignoreError(void* data, const char* message)
{
    (void)data;
    (void)message;
}

static u64 testCounter(void* data)
{
    (void)data;
    return ++Counter;
}

static u64 testFrequency(void* data)
{
    (void)data;
    return 1000000;
}

static const tic_script_error_variable* findVariable(const char* name, const char* scope)
{
    for(s32 i = 0; i < CapturedError.variableCount; i++)
    {
        const tic_script_error_variable* variable = &CapturedError.variables[i];
        if(strcmp(variable->name, name) == 0 && strcmp(variable->scope, scope) == 0)
            return variable;
    }
    return NULL;
}

static bool require(bool condition, const char* message)
{
    if(condition)
        return true;
    fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

int main(void)
{
    static const char Code[] =
        "local multiplier=4\n"
        "local function fail(position,options)\n"
        " local speed=nil\n"
        " local state={enabled=true,count=3,nested={name='ship'}}\n"
        " local longValue=string.rep('x',400)\n"
        " local callback=print\n"
        " local cyclic={} cyclic.self=cyclic\n"
        " return position+speed*multiplier\n"
        "end\n"
        "function TIC() fail(12,{mode='test'}) end\n";

    tic_mem* tic = tic_core_create(44100, TIC80_PIXEL_COLOR_RGBA8888);
    if(!require(tic != NULL, "tic_core_create failed"))
        return 1;
    memcpy(tic->cart.code.data, Code, sizeof Code);

    tic_tick_data tick =
    {
        .error = ignoreError,
        .scriptError = captureScriptError,
        .counter = testCounter,
        .freq = testFrequency,
    };
    tic_core_tick(tic, &tick);
    tic_core_close(tic);

    bool ok = true;
    ok &= require(ErrorCaptured, "runtime error was not captured");
    ok &= require(strstr(CapturedError.message, "(local 'speed')") != NULL,
        "Lua did not identify the failing local");
    ok &= require(CapturedError.frameCount >= 1, "no stack frames captured");
    ok &= require(CapturedError.variableCount >= 7, "expected parameters, locals, and upvalues");

    const tic_script_error_variable* position = findVariable("position", "parameter");
    const tic_script_error_variable* options = findVariable("options", "parameter");
    const tic_script_error_variable* speed = findVariable("speed", "local");
    const tic_script_error_variable* state = findVariable("state", "local");
    const tic_script_error_variable* longValue = findVariable("longValue", "local");
    const tic_script_error_variable* callback = findVariable("callback", "local");
    const tic_script_error_variable* cyclic = findVariable("cyclic", "local");
    const tic_script_error_variable* multiplier = findVariable("multiplier", "upvalue");

    ok &= require(position && strcmp(position->display, "12") == 0, "parameter number preview mismatch");
    ok &= require(options && strstr(options->display, "mode=\"test\"") != NULL,
        "parameter table preview mismatch");
    ok &= require(speed && strcmp(speed->type, "nil") == 0 && strcmp(speed->display, "nil") == 0,
        "nil local preview mismatch");
    ok &= require(state && strstr(state->display, "nested={name=\"ship\"}") != NULL,
        "nested scalar-like table preview mismatch");
    ok &= require(longValue && longValue->valueTruncated && strstr(longValue->display, "...") != NULL,
        "long string preview was not marked truncated");
    ok &= require(callback && strcmp(callback->display, "<function>") == 0,
        "unsupported function fallback mismatch");
    ok &= require(cyclic && strcmp(cyclic->display, "<table>") == 0,
        "cyclic table fallback mismatch");
    ok &= require(multiplier && strcmp(multiplier->display, "4") == 0,
        "upvalue preview mismatch");

    char payloadError[128] = {0};
    char* encoded = NULL;
    ok &= require(tb_script_error_payload_build(
        7, &CapturedError, "md5:test", 1024 * 1024, &encoded, payloadError, sizeof payloadError),
        payloadError[0] ? payloadError : "payload build failed");

    uint8_t* decoded = NULL;
    size_t decodedCapacity = 0;
    size_t decodedLength = 0;
    if(encoded)
    {
        ok &= require(tb_binary_literal_decode(
            encoded,
            strlen(encoded),
            1024 * 1024,
            &decoded,
            &decodedCapacity,
            &decodedLength,
            payloadError,
            sizeof payloadError),
            payloadError[0] ? payloadError : "payload decode failed");
        if(decoded)
        {
            uint8_t* terminated = (uint8_t*)realloc(decoded, decodedLength + 1);
            ok &= require(terminated != NULL, "payload termination allocation failed");
            if(terminated)
            {
                decoded = terminated;
                decoded[decodedLength] = '\0';
                ok &= require(strstr((const char*)decoded, "\"runtimeName\":\"speed\"") != NULL,
                    "payload omitted local variables");
                ok &= require(strstr((const char*)decoded, "\"scope\":\"upvalue\"") != NULL,
                    "payload omitted upvalue scope");
                ok &= require(strstr((const char*)decoded, "\"valueTruncated\":true") != NULL,
                    "payload omitted value truncation");
            }
        }
    }

    free(decoded);
    free(encoded);
    if(!ok)
        return 1;

    puts("script_error locals: PASS");
    return 0;
}
