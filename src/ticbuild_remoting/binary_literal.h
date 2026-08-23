#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Decodes the remoting protocol's <hex...> binary literal syntax into a
// caller-owned reusable buffer. Whitespace between complete bytes is ignored.
bool tb_binary_literal_decode(
    const char* text,
    size_t text_length,
    size_t byte_limit,
    uint8_t** buffer,
    size_t* buffer_capacity,
    size_t* output_length,
    char* err,
    size_t errcap);

// Encodes bytes using the remoting protocol's <hex...> binary literal syntax.
// The returned string is heap-allocated and must be freed by the caller.
bool tb_binary_literal_encode(const uint8_t* bytes, size_t length, size_t output_limit, char** out, char* err, size_t errcap);
