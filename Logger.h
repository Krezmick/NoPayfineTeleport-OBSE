#pragma once

namespace Log
{
    void Init(bool enabled);
    void Write(const char* fmt, ...);
    bool Enabled();
}
