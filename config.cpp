#include "Config.h"

#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>

namespace
{
    Config::Settings g_settings;

    std::string Trim(std::string s)
    {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };

        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    bool ParseBool(const std::string& value, bool fallback)
    {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (v == "1" || v == "true" || v == "yes" || v == "on")
            return true;

        if (v == "0" || v == "false" || v == "no" || v == "off")
            return false;

        return fallback;
    }

    std::string GetPluginDirectory()
    {
        char modulePath[MAX_PATH]{};
        HMODULE module = nullptr;

        if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetPluginDirectory),
            &module))
        {
            DWORD len = GetModuleFileNameA(module, modulePath, MAX_PATH);
            if (len > 0 && len < MAX_PATH)
            {
                std::string path(modulePath, len);
                const auto slash = path.find_last_of("\\/");
                if (slash != std::string::npos)
                    return path.substr(0, slash);
            }
        }

        return ".";
    }
}

namespace Config
{
    const Settings& Get()
    {
        return g_settings;
    }

    bool Load()
    {
        g_settings = Settings{};

        const std::string iniPath = GetPluginDirectory() + "\\NoPayfineTeleport.ini";
        std::ifstream in(iniPath);

        if (!in)
            return true;

        std::string section;

        std::string line;
        while (std::getline(in, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                section = line.substr(1, line.size() - 2);
                std::transform(section.begin(), section.end(), section.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                continue;
            }

            if (section != "general")
                continue;

            const auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));

            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (key == "enabled")
                g_settings.enabled = ParseBool(value, g_settings.enabled);
            else if (key == "log")
                g_settings.log = ParseBool(value, g_settings.log);
            else if (key == "keepstolenitems")
                g_settings.keepStolenItems = ParseBool(value, g_settings.keepStolenItems);
        }

        return true;
    }
}
