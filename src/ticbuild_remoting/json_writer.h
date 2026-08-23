#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct tb_text_buffer tb_text_buffer;

bool tb_json_append_string(tb_text_buffer* out, const char* value, char* err, size_t errcap);
bool tb_json_append_int(tb_text_buffer* out, int64_t value, char* err, size_t errcap);
bool tb_json_append_bool(tb_text_buffer* out, bool value, char* err, size_t errcap);
