#include "Logger.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace
{
    bool g_enabled = true;
    CRITICAL_SECTION g_lock;
    bool g_lockInitialized = false;

    std::string GetLogPath()
    {
        char modulePath[MAX_PATH]{};
        HMODULE module = nullptr;

        if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetLogPath),
            &module))
        {
            DWORD len = GetModuleFileNameA(module, modulePath, MAX_PATH);
            if (len > 0 && len < MAX_PATH)
            {
                std::string path(modulePath, len);
                const auto slash = path.find_last_of("\\/");
                if (slash != std::string::npos)
                    return path.substr(0, slash) + "\\NoPayfineTeleport.log";
            }
        }

        return "NoPayfineTeleport.log";
    }
}

namespace Log
{
    void Init(bool enabled)
    {
        g_enabled = enabled;

        if (!g_lockInitialized)
        {
            InitializeCriticalSection(&g_lock);
            g_lockInitialized = true;
        }

        if (!g_enabled)
            return;

        FILE* f = nullptr;
        fopen_s(&f, GetLogPath().c_str(), "a");
        if (f)
        {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            fprintf(f, "\n[%04u-%02u-%02u %02u:%02u:%02u] NoTeleportToJail initialized\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            fclose(f);
        }
    }

    bool Enabled()
    {
        return g_enabled;
    }

    void Write(const char* fmt, ...)
    {
        if (!g_enabled)
            return;

        if (!g_lockInitialized)
            return;

        EnterCriticalSection(&g_lock);

        FILE* f = nullptr;
        fopen_s(&f, GetLogPath().c_str(), "a");
        if (f)
        {
            va_list args;
            va_start(args, fmt);
            vfprintf(f, fmt, args);
            va_end(args);
            fputc('\n', f);
            fclose(f);
        }

        LeaveCriticalSection(&g_lock);
    }
}
