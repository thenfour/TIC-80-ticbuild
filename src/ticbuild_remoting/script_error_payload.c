#include "ticbuild_remoting/script_error_payload.h"

#include "api.h"
#include "ticbuild_remoting/binary_literal.h"
#include "ticbuild_remoting/json_writer.h"
#include "ticbuild_remoting/utils.h"

static bool append_text_field(tb_text_buffer* json, const char* name, const char* value, char* err, size_t errcap)
{
    return tb_text_buffer_append_cstr(json, name, err, errcap)
        && tb_json_append_string(json, value, err, errcap);
}

static bool append_int_field(tb_text_buffer* json, const char* name, int64_t value, char* err, size_t errcap)
{
    return tb_text_buffer_append_cstr(json, name, err, errcap)
        && tb_json_append_int(json, value, err, errcap);
}

static bool append_variable(tb_text_buffer* json, const tic_script_error_variable* variable, char* err, size_t errcap)
{
    return tb_text_buffer_append_char(json, '{', err, errcap)
        && append_text_field(json, "\"runtimeName\":", variable->name, err, errcap)
        && append_text_field(json, ",\"scope\":", variable->scope, err, errcap)
        && append_text_field(json, ",\"type\":", variable->type, err, errcap)
        && append_text_field(json, ",\"display\":", variable->display, err, errcap)
        && append_int_field(json, ",\"index\":", variable->index, err, errcap)
        && tb_text_buffer_append_cstr(json, ",\"valueTruncated\":", err, errcap)
        && tb_json_append_bool(json, variable->valueTruncated, err, errcap)
        && tb_text_buffer_append_char(json, '}', err, errcap);
}

static bool append_frame(
    tb_text_buffer* json,
    const tic_script_error* error,
    const tic_script_error_frame* frame,
    char* err,
    size_t errcap)
{
    bool ok = tb_text_buffer_append_char(json, '{', err, errcap)
        && append_text_field(json, "\"source\":", frame->source, err, errcap)
        && append_text_field(json, ",\"name\":", frame->name, err, errcap)
        && append_text_field(json, ",\"nameWhat\":", frame->nameWhat, err, errcap)
        && append_text_field(json, ",\"what\":", frame->what, err, errcap)
        && append_int_field(json, ",\"currentLine\":", frame->currentLine, err, errcap)
        && append_int_field(json, ",\"lineDefined\":", frame->lineDefined, err, errcap)
        && append_int_field(json, ",\"lastLineDefined\":", frame->lastLineDefined, err, errcap)
        && append_int_field(json, ",\"parameterCount\":", frame->parameterCount, err, errcap)
        && append_int_field(json, ",\"upvalueCount\":", frame->upvalueCount, err, errcap)
        && tb_text_buffer_append_cstr(json, ",\"variadic\":", err, errcap)
        && tb_json_append_bool(json, frame->variadic, err, errcap)
        && tb_text_buffer_append_cstr(json, ",\"tailCall\":", err, errcap)
        && tb_json_append_bool(json, frame->tailCall, err, errcap)
        && tb_text_buffer_append_cstr(json, ",\"variablesCaptured\":", err, errcap)
        && tb_json_append_bool(json, frame->variablesCaptured, err, errcap)
        && tb_text_buffer_append_cstr(json, ",\"variablesTruncated\":", err, errcap)
        && tb_json_append_bool(json, frame->variablesTruncated, err, errcap)
        && tb_text_buffer_append_cstr(json, ",\"variables\":[", err, errcap);

    if(frame->variableStart < 0 || frame->variableCount < 0
        || frame->variableStart > error->variableCount
        || frame->variableCount > error->variableCount - frame->variableStart)
    {
        tb_set_err(err, errcap, "invalid script error variables");
        return false;
    }
    const s32 variableEnd = frame->variableStart + frame->variableCount;

    for(s32 i = frame->variableStart; ok && i < variableEnd; i++)
    {
        if(i > frame->variableStart)
            ok = tb_text_buffer_append_char(json, ',', err, errcap);
        if(ok)
            ok = append_variable(json, &error->variables[i], err, errcap);
    }

    return ok && tb_text_buffer_append_cstr(json, "]}", err, errcap);
}

bool tb_script_error_payload_build(
    uint64_t error_id,
    const tic_script_error* error,
    const char* code_hash,
    size_t output_limit,
    char** out,
    char* err,
    size_t errcap)
{
    if(!out || !error || output_limit < 3
        || error->variableCount < 0 || error->variableCount > TIC_SCRIPT_ERROR_MAX_VARIABLES)
    {
        tb_set_err(err, errcap, "invalid script error payload");
        return false;
    }

    *out = NULL;
    tb_text_buffer json;
    tb_text_buffer_init(&json, (output_limit - 3) / 2);

    bool ok = tb_text_buffer_append_cstr(&json, "{\"schemaVersion\":1,\"errorId\":", err, errcap)
        && tb_json_append_int(&json, (int64_t)error_id, err, errcap)
        && append_text_field(&json, ",\"language\":", error->language, err, errcap)
        && append_text_field(&json, ",\"kind\":", error->kind, err, errcap)
        && append_text_field(&json, ",\"phase\":", error->phase, err, errcap)
        && append_text_field(&json, ",\"message\":", error->message, err, errcap)
        && append_text_field(&json, ",\"traceback\":", error->traceback, err, errcap)
        && append_text_field(&json, ",\"codeHash\":", code_hash, err, errcap)
        && tb_text_buffer_append_cstr(&json, ",\"framesTruncated\":", err, errcap)
        && tb_json_append_bool(&json, error->framesTruncated, err, errcap)
        && tb_text_buffer_append_cstr(&json, ",\"frames\":[", err, errcap);

    for(s32 i = 0; ok && i < error->frameCount; i++)
    {
        if(i)
            ok = tb_text_buffer_append_char(&json, ',', err, errcap);
        if(ok)
            ok = append_frame(&json, error, &error->frames[i], err, errcap);
    }

    if(ok)
        ok = tb_text_buffer_append_cstr(&json, "]}", err, errcap);
    if(ok)
        ok = tb_binary_literal_encode((const uint8_t*)json.ptr, json.len, output_limit, out, err, errcap);

    tb_text_buffer_dispose(&json);
    return ok;
}
