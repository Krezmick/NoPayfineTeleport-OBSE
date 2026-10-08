#include "obse/PluginAPI.h"
#include "obse/GameAPI.h"

#include "Config.h"
#include "Hook.h"
#include "Logger.h"

PluginHandle g_pluginHandle = kPluginHandle_Invalid;

namespace
{
    void ReinstallForCurrentPlayer(const char* reason)
    {
        Hook::Uninstall();

        if (!Config::Get().enabled)
        {
            Log::Write("Hook not installed (%s): plugin disabled.", reason);
            return;
        }

        if (!Hook::Install())
            Log::Write("Hook installation deferred/failed (%s).", reason);
    }

    void MessageHandler(OBSEMessagingInterface::Message* msg)
    {
        if (!msg)
            return;

        switch (msg->type)
        {
        case OBSEMessagingInterface::kMessage_PreLoadGame:
            Log::Write("PreLoadGame received; removing player hook before load.");
            Hook::Uninstall();
            break;

        case OBSEMessagingInterface::kMessage_LoadGame:
            ReinstallForCurrentPlayer("LoadGame");
            break;

        case OBSEMessagingInterface::kMessage_SaveGame:
            if (Config::Get().enabled && !Hook::IsInstalled())
                ReinstallForCurrentPlayer("SaveGame");
            break;

        default:
            break;
        }
    }
}

extern "C"
{
    bool OBSEPlugin_Query(const OBSEInterface* obse, PluginInfo* info)
    {
        if (!obse || !info)
            return false;

        info->infoVersion = PluginInfo::kInfoVersion;
        info->name = "NoPayfineTeleport";
        info->version = 1;

        if (obse->isEditor)
            return true;

#ifndef OBLIVION_VERSION
#error "The OBSE SDK must define OBLIVION_VERSION."
#endif
        if (obse->oblivionVersion != OBLIVION_VERSION)
            return false;

        if (obse->obseVersion < 20)
            return false;

        return true;
    }

    bool OBSEPlugin_Load(const OBSEInterface* obse)
    {
        if (!obse)
            return false;

        g_pluginHandle = obse->GetPluginHandle();

        Config::Load();
        Log::Init(Config::Get().log);

        Log::Write(
            "NoPayfineTeleport v1.0 loading: OBSE=%u Oblivion=0x%08X enabled=%u keepStolenItems=%u",
            obse->obseVersion,
            obse->oblivionVersion,
            Config::Get().enabled ? 1u : 0u,
            Config::Get().keepStolenItems ? 1u : 0u);

        if (obse->isEditor)
            return true;

        OBSEMessagingInterface* msg =
            static_cast<OBSEMessagingInterface*>(
                obse->QueryInterface(kInterface_Messaging));

        if (!msg)
        {
            Log::Write("ERROR: messaging interface unavailable.");
            return false;
        }

        if (!msg->RegisterListener(g_pluginHandle, "OBSE", MessageHandler))
        {
            Log::Write("ERROR: failed to register OBSE message listener.");
            return false;
        }

        if (Config::Get().enabled)
        {
            if (!Hook::Install())
                Log::Write("Initial hook installation deferred; waiting for player lifecycle message.");
        }
        else
        {
            Log::Write("Plugin loaded disabled by INI.");
        }

        Log::Write("NoPayfineTeleport v1.0 loaded successfully.");
        return true;
    }
}
