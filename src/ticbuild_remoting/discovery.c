#include "ticbuild_remoting/discovery.h"

#if defined(_WIN32) || defined(__TIC_WINDOWS__)

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char g_discovery_path_global[MAX_PATH] = {0};
static char g_discovery_path_custom[MAX_PATH] = {0};
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

static bool tb_write_discovery_file(const char* dir, int port, const char* json, size_t json_len, char* outpath, size_t outcap, char* err, size_t errcap)
{
    if(!dir || !dir[0])
        return false;

    if(!tb_ensure_dir(dir, err, errcap))
        return false;

    DWORD pid = GetCurrentProcessId();
    if(!outpath || outcap == 0)
        return false;

    int plen = snprintf(outpath, outcap, "%s\\tic80-remote.%lu.json", dir, (unsigned long)pid);
    if(plen < 0 || (size_t)plen >= outcap)
    {
        tb_set_err(err, errcap, "discovery file path too long");
        return false;
    }

    FILE* f = fopen(outpath, "wb");
    if(!f)
    {
        tb_set_err(err, errcap, "failed to write discovery file");
        return false;
    }

    fwrite(json, 1, json_len, f);
    fclose(f);

    printf("[remoting] discovery file written: %s\n", outpath);

    return true;
}

bool tb_discovery_start(int port, const char* session_dir, bool global_disco, char* err, size_t errcap)
{
    if(g_discovery_active)
        return true;

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
        (unsigned long)GetCurrentProcessId(), port, ts, TB_REMOTING_PROTOCOL_VERSION_STRING);

    if(jlen < 0 || (size_t)jlen >= sizeof json)
    {
        tb_set_err(err, errcap, "discovery json too large");
        return false;
    }

    bool wrote_any = false;

    if(session_dir && session_dir[0])
    {
        if(tb_write_discovery_file(session_dir, port, json, (size_t)jlen, g_discovery_path_custom, sizeof g_discovery_path_custom, err, errcap))
            wrote_any = true;
    }

    if(global_disco)
    {
        const char* local = getenv("LOCALAPPDATA");
        if(!local || !local[0])
        {
            tb_set_err(err, errcap, "LOCALAPPDATA not set");
            if(!wrote_any) return false;
        }
        else
        {
            char dir[MAX_PATH];
            int dlen = snprintf(dir, sizeof dir, "%s\\TIC-80\\remoting\\sessions", local);
            if(dlen < 0 || (size_t)dlen >= sizeof dir)
            {
                tb_set_err(err, errcap, "discovery path too long");
                if(!wrote_any) return false;
            }
            else if(tb_write_discovery_file(dir, port, json, (size_t)jlen, g_discovery_path_global, sizeof g_discovery_path_global, err, errcap))
                wrote_any = true;
        }
    }

    if(!wrote_any)
        return false;

    g_discovery_active = true;
    return true;
}

void tb_discovery_stop(void)
{
    if(!g_discovery_active)
        return;

    if(g_discovery_path_global[0])
        DeleteFileA(g_discovery_path_global);
    if(g_discovery_path_custom[0])
        DeleteFileA(g_discovery_path_custom);

    g_discovery_path_global[0] = '\0';
    g_discovery_path_custom[0] = '\0';
    g_discovery_active = false;
}

#endif
