#pragma once

namespace Config
{
    struct Settings
    {
        bool enabled = true;
        bool log = true;
        bool keepStolenItems = false;
    };

    const Settings& Get();
    bool Load();
}
