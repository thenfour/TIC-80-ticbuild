#include "ticbuild_remoting/discovery.h"

#if defined(_WIN32) || defined(__TIC_WINDOWS__)

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char g_discovery_path[MAX_PATH] = {0};
static bool g_discovery_active = false;

static void tb_set_err(char* err, size_t errcap, const char* msg)
{
    if(!err || errcap == 0) return;
    if(!msg) msg = "error";
    strncpy(err, msg, errcap - 1);
    err[errcap - 1] = '\0';
}

static bool tb_ensure_dir(const char* path, char* err, size_t errcap)
{
    if(!path || !path[0])
    {
        tb_set_err(err, errcap, "invalid path");
        return false;
    }

    char tmp[MAX_PATH];
    size_t len = strlen(path);
    if(len >= sizeof tmp)
    {
        tb_set_err(err, errcap, "path too long");
        return false;
    }

    strncpy(tmp, path, sizeof tmp);
    tmp[sizeof tmp - 1] = '\0';

    for(size_t i = 1; i < len; i++)
    {
        if(tmp[i] == '\\' || tmp[i] == '/')
        {
            char saved = tmp[i];
            tmp[i] = '\0';
            if(tmp[0] != '\0')
            {
                if(!CreateDirectoryA(tmp, NULL))
                {
                    DWORD e = GetLastError();
                    if(e != ERROR_ALREADY_EXISTS)
                    {
                        tb_set_err(err, errcap, "failed to create directory");
                        return false;
                    }
                }
            }
            tmp[i] = saved;
        }
    }

    if(!CreateDirectoryA(tmp, NULL))
    {
        DWORD e = GetLastError();
        if(e != ERROR_ALREADY_EXISTS)
        {
            tb_set_err(err, errcap, "failed to create directory");
            return false;
        }
    }

    return true;
}

bool tb_discovery_start(int port, char* err, size_t errcap)
{
    if(g_discovery_active)
        return true;

    const char* local = getenv("LOCALAPPDATA");
    if(!local || !local[0])
    {
        tb_set_err(err, errcap, "LOCALAPPDATA not set");
        return false;
    }

    char dir[MAX_PATH];
    int dlen = snprintf(dir, sizeof dir, "%s\\TIC-80\\remoting\\sessions", local);
    if(dlen < 0 || (size_t)dlen >= sizeof dir)
    {
        tb_set_err(err, errcap, "discovery path too long");
        return false;
    }

    if(!tb_ensure_dir(dir, err, errcap))
        return false;

    DWORD pid = GetCurrentProcessId();
    char path[MAX_PATH];
    int plen = snprintf(path, sizeof path, "%s\\tic80-remote.%lu.json", dir, (unsigned long)pid);
    if(plen < 0 || (size_t)plen >= sizeof path)
    {
        tb_set_err(err, errcap, "discovery file path too long");
        return false;
    }

    SYSTEMTIME st;
    GetSystemTime(&st);
    char ts[32];
    snprintf(ts, sizeof ts, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    char json[512];
    int jlen = snprintf(json, sizeof json,
        "{\n"
        "  \"pid\": %lu,\n"
        "  \"host\": \"127.0.0.1\",\n"
        "  \"port\": %d,\n"
        "  \"startedAt\": \"%s\",\n"
        "  \"remotingVersion\": \"%s\"\n"
        "}\n",
        (unsigned long)pid, port, ts, TB_REMOTING_PROTOCOL_VERSION_STRING);

    if(jlen < 0 || (size_t)jlen >= sizeof json)
    {
        tb_set_err(err, errcap, "discovery json too large");
        return false;
    }

    FILE* f = fopen(path, "wb");
    if(!f)
    {
        tb_set_err(err, errcap, "failed to write discovery file");
        return false;
    }

    fwrite(json, 1, (size_t)jlen, f);
    fclose(f);

    strncpy(g_discovery_path, path, sizeof g_discovery_path);
    g_discovery_path[sizeof g_discovery_path - 1] = '\0';
    g_discovery_active = true;
    return true;
}

void tb_discovery_stop(void)
{
    if(!g_discovery_active)
        return;

    if(g_discovery_path[0])
        DeleteFileA(g_discovery_path);

    g_discovery_path[0] = '\0';
    g_discovery_active = false;
}

#endif
