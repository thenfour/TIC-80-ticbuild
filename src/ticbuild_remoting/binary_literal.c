#include "ticbuild_remoting/binary_literal.h"

#include "ticbuild_remoting/utils.h"

#include <stdint.h>
#include <stdlib.h>

bool tb_binary_literal_decode(
    const char* text,
    size_t text_length,
    size_t byte_limit,
    uint8_t** buffer,
    size_t* buffer_capacity,
    size_t* output_length,
    char* err,
    size_t errcap)
{
    if(!text || text_length < 2 || text[0] != '<' || text[text_length - 1] != '>'
        || !buffer || !buffer_capacity || !output_length)
    {
        tb_set_err(err, errcap, "invalid binary literal");
        return false;
    }

    char* hex = (char*)malloc(text_length);
    if(!hex)
    {
        tb_set_err(err, errcap, "out of memory");
        return false;
    }

    size_t hex_length = 0;
    for(size_t i = 1; i + 1 < text_length; i++)
    {
        const char c = text[i];
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;

        uint8_t nibble;
        if(!tb_hex_nibble(c, &nibble))
        {
            free(hex);
            tb_set_err(err, errcap, "binary contains non-hex");
            return false;
        }
        hex[hex_length++] = c;
    }

    if(hex_length % 2 != 0)
    {
        free(hex);
        tb_set_err(err, errcap, "binary hex digit count must be even");
        return false;
    }

    const size_t byte_length = hex_length / 2;
    if(byte_length > byte_limit)
    {
        free(hex);
        tb_set_err(err, errcap, "binary too large");
        return false;
    }

    if(*buffer_capacity < byte_length)
    {
        uint8_t* next = (uint8_t*)realloc(*buffer, byte_length);
        if(!next)
        {
            free(hex);
            tb_set_err(err, errcap, "out of memory");
            return false;
        }
        *buffer = next;
        *buffer_capacity = byte_length;
    }

    for(size_t i = 0; i < byte_length; i++)
    {
        uint8_t hi;
        uint8_t lo;
        if(!tb_hex_nibble(hex[i * 2], &hi) || !tb_hex_nibble(hex[i * 2 + 1], &lo))
        {
            free(hex);
            tb_set_err(err, errcap, "invalid hex");
            return false;
        }
        (*buffer)[i] = (uint8_t)((hi << 4) | lo);
    }

    free(hex);
    *output_length = byte_length;
    return true;
}

bool tb_binary_literal_encode(const uint8_t* bytes, size_t length, size_t output_limit, char** out, char* err, size_t errcap)
{
    static const char Hex[] = "0123456789abcdef";

    if(!out || (!bytes && length > 0))
    {
        tb_set_err(err, errcap, "invalid binary data");
        return false;
    }

    *out = NULL;
    if(length > (SIZE_MAX - 3) / 2)
    {
        tb_set_err(err, errcap, "binary too large");
        return false;
    }

    const size_t output_size = length * 2 + 3;
    if(output_size > output_limit)
    {
        tb_set_err(err, errcap, "binary too large");
        return false;
    }

    char* encoded = (char*)malloc(output_size);
    if(!encoded)
    {
        tb_set_err(err, errcap, "out of memory");
        return false;
    }

    encoded[0] = '<';
    for(size_t i = 0; i < length; i++)
    {
        const uint8_t byte = bytes[i];
        encoded[1 + i * 2] = Hex[byte >> 4];
        encoded[2 + i * 2] = Hex[byte & 0xf];
    }
    encoded[output_size - 2] = '>';
    encoded[output_size - 1] = '\0';

    *out = encoded;
    return true;
}
