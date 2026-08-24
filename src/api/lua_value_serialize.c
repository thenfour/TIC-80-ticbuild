#include "api/lua_value_serialize.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <lauxlib.h>

#define LUAAPI_SERIALIZE_MAX_DEPTH 32

typedef struct
{
    luaapi_value_write write;
    void* writeData;
    char* err;
    size_t errcap;
} luaapi_serialize_context;

static void setError(luaapi_serialize_context* context, const char* message)
{
    if(context->err && context->errcap)
    {
        strncpy(context->err, message ? message : "error", context->errcap - 1);
        context->err[context->errcap - 1] = '\0';
    }
}

static void setErrorFormat(luaapi_serialize_context* context, const char* format, const char* value)
{
    if(context->err && context->errcap)
    {
        snprintf(context->err, context->errcap, format, value ? value : "");
        context->err[context->errcap - 1] = '\0';
    }
}

static bool append(luaapi_serialize_context* context, const char* text, size_t length)
{
    if(context->write(context->writeData, text, length))
        return true;

    if(context->err && context->errcap && !context->err[0])
        setError(context, "output failed");
    return false;
}

static bool appendChar(luaapi_serialize_context* context, char value)
{
    return append(context, &value, 1);
}

static bool appendString(luaapi_serialize_context* context, const char* value)
{
    return append(context, value, strlen(value));
}

static bool isIdentifier(const char* text, size_t length)
{
    if(length == 0)
        return false;

    unsigned char value = (unsigned char)text[0];
    if(!(isalpha(value) || value == '_'))
        return false;

    for(size_t i = 1; i < length; i++)
    {
        value = (unsigned char)text[i];
        if(!(isalnum(value) || value == '_'))
            return false;
    }

    return true;
}

static bool serializeValue(lua_State* lua, int index, luaapi_serialize_context* context, int depth, int visitedIndex);

static bool serializeString(const char* value, size_t length, luaapi_serialize_context* context)
{
    if(!appendChar(context, '"'))
        return false;

    for(size_t i = 0; i < length; i++)
    {
        char byte = value ? value[i] : '\0';
        if((unsigned char)byte > 0x7f)
            byte = '?';

        if(byte == '\\' || byte == '"')
        {
            if(!appendChar(context, '\\') || !appendChar(context, byte))
                return false;
        }
        else
        {
            unsigned char ascii = (unsigned char)byte;
            if(!(ascii == '\t' || ascii == ' ' || (ascii >= 0x21 && ascii <= 0x7e)))
                byte = '?';
            if(!appendChar(context, byte))
                return false;
        }
    }

    return appendChar(context, '"');
}

static void trimTrailingZeros(char* text)
{
    char* dot = strchr(text, '.');
    if(!dot)
        return;

    char* end = text + strlen(text) - 1;
    while(end > dot && *end == '0')
        *end-- = '\0';
    if(end == dot)
        *end = '\0';
}

static bool serializeNumber(lua_State* lua, int index, luaapi_serialize_context* context)
{
#if LUA_VERSION_NUM >= 503
    if(lua_isinteger(lua, index))
    {
        char buffer[64];
        snprintf(buffer, sizeof buffer, "%lld", (long long)lua_tointeger(lua, index));
        return appendString(context, buffer);
    }
#endif

    const double number = (double)lua_tonumber(lua, index);
#if defined(_MSC_VER)
    if(!_finite(number))
#else
    if(!isfinite(number))
#endif
    {
        setError(context, "unsupported number value");
        return false;
    }

    char buffer[512];
    const int length = snprintf(buffer, sizeof buffer, "%.17f", number);
    if(length < 0 || (size_t)length >= sizeof buffer)
    {
        setError(context, "number too large");
        return false;
    }

    trimTrailingZeros(buffer);
    return appendString(context, buffer);
}

static bool serializeTable(lua_State* lua, int index, luaapi_serialize_context* context, int depth, int visitedIndex)
{
    if(depth > LUAAPI_SERIALIZE_MAX_DEPTH)
    {
        setError(context, "table too deep");
        return false;
    }

    index = lua_absindex(lua, index);
    visitedIndex = lua_absindex(lua, visitedIndex);

    lua_pushvalue(lua, index);
    lua_rawget(lua, visitedIndex);
    if(!lua_isnil(lua, -1))
    {
        lua_pop(lua, 1);
        setError(context, "cycle detected");
        return false;
    }
    lua_pop(lua, 1);

    lua_pushvalue(lua, index);
    lua_pushboolean(lua, 1);
    lua_rawset(lua, visitedIndex);

    if(!appendChar(context, '{'))
        return false;

    const size_t arrayLength = (size_t)lua_rawlen(lua, index);
    bool hasItems = false;

    for(size_t i = 1; i <= arrayLength; i++)
    {
        if(hasItems && !appendChar(context, ','))
            return false;

        lua_rawgeti(lua, index, (lua_Integer)i);
        if(!serializeValue(lua, -1, context, depth + 1, visitedIndex))
        {
            lua_pop(lua, 1);
            return false;
        }
        lua_pop(lua, 1);
        hasItems = true;
    }

    lua_pushnil(lua);
    while(lua_next(lua, index) != 0)
    {
        bool skip = false;

#if LUA_VERSION_NUM >= 503
        if(lua_isinteger(lua, -2))
        {
            const lua_Integer key = lua_tointeger(lua, -2);
            if(key >= 1 && (size_t)key <= arrayLength)
                skip = true;
        }
#endif

        if(!skip)
        {
            const int keyType = lua_type(lua, -2);
            if(keyType == LUA_TTABLE || keyType == LUA_TFUNCTION || keyType == LUA_TTHREAD
                || keyType == LUA_TUSERDATA || keyType == LUA_TLIGHTUSERDATA)
            {
                lua_pop(lua, 2);
                setError(context, "unsupported key type");
                return false;
            }

            if(hasItems && !appendChar(context, ','))
            {
                lua_pop(lua, 2);
                return false;
            }

            if(keyType == LUA_TSTRING)
            {
                size_t keyLength = 0;
                const char* key = lua_tolstring(lua, -2, &keyLength);
                if(key && isIdentifier(key, keyLength))
                {
                    if(!append(context, key, keyLength) || !appendChar(context, '='))
                    {
                        lua_pop(lua, 2);
                        return false;
                    }
                }
                else if(!appendChar(context, '[')
                    || !serializeValue(lua, -2, context, depth + 1, visitedIndex)
                    || !appendString(context, "]="))
                {
                    lua_pop(lua, 2);
                    return false;
                }
            }
            else if(!appendChar(context, '[')
                || !serializeValue(lua, -2, context, depth + 1, visitedIndex)
                || !appendString(context, "]="))
            {
                lua_pop(lua, 2);
                return false;
            }

            if(!serializeValue(lua, -1, context, depth + 1, visitedIndex))
            {
                lua_pop(lua, 2);
                return false;
            }

            hasItems = true;
        }

        lua_pop(lua, 1);
    }

    return appendChar(context, '}');
}

static bool serializeValue(lua_State* lua, int index, luaapi_serialize_context* context, int depth, int visitedIndex)
{
    index = lua_absindex(lua, index);

    switch(lua_type(lua, index))
    {
    case LUA_TNIL:
        return appendString(context, "nil");
    case LUA_TBOOLEAN:
        return appendString(context, lua_toboolean(lua, index) ? "true" : "false");
    case LUA_TNUMBER:
        return serializeNumber(lua, index, context);
    case LUA_TSTRING:
    {
        size_t length = 0;
        const char* value = lua_tolstring(lua, index, &length);
        return serializeString(value ? value : "", length, context);
    }
    case LUA_TTABLE:
        return serializeTable(lua, index, context, depth, visitedIndex);
    default:
    {
        const char* typeName = luaL_typename(lua, index);
        if(typeName)
            setErrorFormat(context, "unsupported result type: %s", typeName);
        else
            setError(context, "unsupported result type");
        return false;
    }
    }
}

bool luaapi_serialize_value(
    lua_State* lua,
    int index,
    luaapi_value_write write,
    void* writeData,
    char* err,
    size_t errcap)
{
    if(err && errcap)
        err[0] = '\0';
    if(!lua || !write)
    {
        if(err && errcap)
        {
            strncpy(err, "invalid serialization request", errcap - 1);
            err[errcap - 1] = '\0';
        }
        return false;
    }

    const int originalTop = lua_gettop(lua);
    const int absoluteIndex = lua_absindex(lua, index);
    luaapi_serialize_context context =
    {
        .write = write,
        .writeData = writeData,
        .err = err,
        .errcap = errcap,
    };

    lua_newtable(lua);
    const int visitedIndex = lua_gettop(lua);
    const bool ok = serializeValue(lua, absoluteIndex, &context, 0, visitedIndex);

    // Recursive table serialization can stop at any depth when an output budget
    // is reached. Restore the exact caller stack instead of relying on each
    // early-return path to unwind its temporary keys and values.
    lua_settop(lua, originalTop);
    return ok;
}
