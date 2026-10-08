#include "Hook.h"
#include "Config.h"
#include "Logger.h"

#include "obse/GameAPI.h"
#include "obse/GameObjects.h"

#include <windows.h>
#include <cstdint>

namespace
{
    //GoToPrison = 0xB9, ServePrisonTime = 0xBA, PayFine = 0xBB.
    constexpr std::size_t kPayFineVtableIndex = 0xBB;

    using PayFineFn = void(__thiscall*)(Actor*, TESFaction*, bool, bool);

    PayFineFn g_originalPayFine = nullptr;
    void** g_playerVtable = nullptr;
    void* g_previousSlot = nullptr;
    bool g_installed = false;

    bool IsExecutableAddress(void* address)
    {
        if (!address)
            return false;

        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(address, &mbi, sizeof(mbi)))
            return false;

        if (mbi.State != MEM_COMMIT)
            return false;

        const DWORD p = mbi.Protect & 0xFF;
        return p == PAGE_EXECUTE ||
            p == PAGE_EXECUTE_READ ||
            p == PAGE_EXECUTE_READWRITE ||
            p == PAGE_EXECUTE_WRITECOPY;
    }

    bool MakeSlotWritable(void* slot, DWORD& oldProtect)
    {
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
        {
            Log::Write("ERROR: VirtualProtect failed. Win32=%lu", GetLastError());
            return false;
        }
        return true;
    }

    void RestoreProtection(void* slot, DWORD oldProtect)
    {
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    }

    void __fastcall HookedPayFine(
        Actor* self,
        void* /*edx*/,
        TESFaction* faction,
        bool goToJail,
        bool removeStolenItems)
    {
        if (Config::Get().enabled && self == reinterpret_cast<Actor*>(*g_thePlayer))
        {
            const bool originalGoToJail = goToJail;
            const bool originalRemoveStolenItems = removeStolenItems;

            goToJail = false;
            if (Config::Get().keepStolenItems)
                removeStolenItems = false;

            Log::Write(
                "PayFine: jail=%u->%u stolen=%u->%u",
                originalGoToJail ? 1u : 0u,
                goToJail ? 1u : 0u,
                originalRemoveStolenItems ? 1u : 0u,
                removeStolenItems ? 1u : 0u);
        }

        if (g_originalPayFine)
            g_originalPayFine(self, faction, goToJail, removeStolenItems);
    }
}

namespace Hook
{
    bool Install()
    {
        if (g_installed)
            return true;

        if (!g_thePlayer)
        {
            Log::Write("ERROR: g_thePlayer symbol is null.");
            return false;
        }

        PlayerCharacter* player = *g_thePlayer;
        if (!player)
        {
            Log::Write("PayFine hook deferred: player object is not initialized.");
            return false;
        }

        void*** playerObject = reinterpret_cast<void***>(player);
        if (!playerObject || !*playerObject)
        {
            Log::Write("ERROR: player vtable pointer is null.");
            return false;
        }

        void** vtable = *playerObject;
        void* slot = &vtable[kPayFineVtableIndex];
        void* original = vtable[kPayFineVtableIndex];

        if (!IsExecutableAddress(original))
        {
            Log::Write(
                "ERROR: PayFine slot 0x%zX is not executable (%p); refusing to hook.",
                kPayFineVtableIndex,
                original);
            return false;
        }

        if (original == reinterpret_cast<void*>(&HookedPayFine))
        {
            Log::Write("PayFine slot already points at our hook; treating as installed.");
            g_playerVtable = vtable;
            g_previousSlot = g_originalPayFine ? reinterpret_cast<void*>(g_originalPayFine) : nullptr;
            g_installed = true;
            return true;
        }

        if (g_previousSlot && original != g_previousSlot)
        {
            Log::Write(
                "ERROR: PayFine slot changed since our last install (%p -> %p); refusing to chain unknown hook.",
                g_previousSlot,
                original);
            return false;
        }

        DWORD oldProtect = 0;
        if (!MakeSlotWritable(slot, oldProtect))
            return false;

        void* expected = original;
        void* replacement = reinterpret_cast<void*>(&HookedPayFine);
        void* observed = InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID*>(slot), replacement, expected);

        RestoreProtection(slot, oldProtect);

        if (observed != expected)
        {
            Log::Write(
                "ERROR: PayFine slot changed concurrently (%p != expected %p); hook not installed.",
                observed,
                expected);
            return false;
        }

        g_playerVtable = vtable;
        g_previousSlot = original;
        g_originalPayFine = reinterpret_cast<PayFineFn>(original);
        g_installed = true;

        Log::Write(
            "PayFine hook installed: player=%p vtable=%p slot=0x%zX original=%p hook=%p",
            player,
            vtable,
            kPayFineVtableIndex,
            original,
            replacement);

        return true;
    }

    void Uninstall()
    {
        if (!g_installed || !g_playerVtable || !g_previousSlot)
            return;

        void** slot = &g_playerVtable[kPayFineVtableIndex];
        void* current = *slot;

        if (current != reinterpret_cast<void*>(&HookedPayFine))
        {
            Log::Write(
                "PayFine hook not restored: slot no longer belongs to us (current=%p).",
                current);
            g_originalPayFine = nullptr;
            g_playerVtable = nullptr;
            g_previousSlot = nullptr;
            g_installed = false;
            return;
        }

        DWORD oldProtect = 0;
        if (MakeSlotWritable(slot, oldProtect))
        {
            InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID*>(slot),
                g_previousSlot,
                reinterpret_cast<void*>(&HookedPayFine));
            RestoreProtection(slot, oldProtect);
        }

        g_originalPayFine = nullptr;
        g_playerVtable = nullptr;
        g_previousSlot = nullptr;
        g_installed = false;
    }

    bool IsInstalled()
    {
        return g_installed;
    }
}
