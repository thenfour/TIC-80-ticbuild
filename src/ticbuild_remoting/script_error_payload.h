#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct tic_script_error;

// Builds the complete remoting payload: UTF-8 JSON wrapped in a binary literal.
// The returned string is heap-allocated and must be freed by the caller.
bool tb_script_error_payload_build(
    uint64_t error_id,
    const struct tic_script_error* error,
    const char* code_hash,
    size_t output_limit,
    char** out,
    char* err,
    size_t errcap);
