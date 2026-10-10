#include <windows.h>
#include <shlobj.h>
#include <string.h>

#include "common/IDebugLog.h"
#include "obse/PluginAPI.h"
#include "obse/CommandTable.h"
#include "obse/GameAPI.h"
#include "obse/GameObjects.h"
#include "obse_common/obse_version.h"

#ifndef PASS_COMMAND_ARGS
#define PASS_COMMAND_ARGS paramInfo, arg1, thisObj, arg3, scriptObj, eventList, result, opcodeOffsetPtr
#endif

IDebugLog gLog;

static PluginHandle                 g_pluginHandle = kPluginHandle_Invalid;
static OBSECommandTableInterface* g_cmdTable = NULL;

typedef bool (*CommandExecute)(COMMAND_ARGS);
static CommandExecute g_origPayFine = NULL;

//v1.2.0.416
static const UInt32 kAddr_PayFineWorker = 0x00670CA0;   // player method
static const UInt32 kOffset_JailPending = 0x12C;        // byte on the player

static const UInt8 kSig_PayFineCommand[] = {
	0x8B, 0x44, 0x24, 0x0C,             // mov eax, [esp+0Ch]   (thisObj)
	0x85, 0xC0,                         // test eax, eax
	0x75, 0x03,                         // jnz +3
	0x32, 0xC0,                         // xor al, al
	0xC3                                // ret
};

static const UInt8 kSig_PayFineWorker[] = {
	0x83, 0xEC, 0x0C,                   // sub esp, 0Ch
	0x80, 0x7C, 0x24, 0x10, 0x00,       // cmp byte ptr [esp+10h], 0
	0x53, 0x55, 0x56, 0x57,             // push ebx / ebp / esi / edi
	0x8B, 0xF1,                         // mov esi, ecx
	0x0F, 0x85, 0xB5, 0x00, 0x00, 0x00  // jnz +0B5h
};

static bool CodeMatches(UInt32 addr, const UInt8* sig, size_t len)
{
	return memcmp((const void*)addr, sig, len) == 0;
}

static void CallPayFineWorker(void* player, UInt32 bDeferredJailStep)
{
	const UInt32 fn = kAddr_PayFineWorker;

	__asm {
		mov  eax, fn
		push bDeferredJailStep
		mov  ecx, player
		call eax
	}
}

static bool Hook_PayFine(COMMAND_ARGS)
{
	PlayerCharacter* player = *g_thePlayer;

	if (player && thisObj == player) {
		_MESSAGE("PayFine on player: running paperwork without arming the jail teleport");
		CallPayFineWorker(player, 0);
		*((UInt8*)player + kOffset_JailPending) = 0;
		return true;
	}

	return g_origPayFine(PASS_COMMAND_ARGS);
}

static bool InstallHook()
{
	const CommandInfo* info = g_cmdTable->GetByName("PayFine");
	if (!info) {
		_ERROR("could not find the PayFine command, plugin inactive");
		return false;
	}

	CommandInfo* cmd = const_cast<CommandInfo*>(info);
	UInt32 execAddr = (UInt32)cmd->execute;

	if (!CodeMatches(execAddr, kSig_PayFineCommand, sizeof(kSig_PayFineCommand))) {
		_ERROR("PayFine code at %08X does not look like 1.2.0.416, plugin inactive", execAddr);
		return false;
	}
    if (!CodeMatches(kAddr_PayFineWorker, kSig_PayFineWorker, sizeof(kSig_PayFineWorker))) {
		_ERROR("player method at %08X does not look like 1.2.0.416, plugin inactive", kAddr_PayFineWorker);
		return false;
	}
	DWORD oldProtect;
	if (!VirtualProtect(&cmd->execute, sizeof(cmd->execute), PAGE_READWRITE, &oldProtect)) {
		_ERROR("VirtualProtect failed (%lu), plugin inactive", GetLastError());
		return false;
	}

	g_origPayFine = reinterpret_cast<CommandExecute>(cmd->execute);
	cmd->execute = reinterpret_cast<decltype(cmd->execute)>(&Hook_PayFine);

	VirtualProtect(&cmd->execute, sizeof(cmd->execute), oldProtect, &oldProtect);

	_MESSAGE("PayFine (%08X) hooked, worker at %08X", execAddr, kAddr_PayFineWorker);
	return true;
}

extern "C" {

	__declspec(dllexport) bool OBSEPlugin_Query(const OBSEInterface* obse, PluginInfo* info)
	{
		gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Oblivion\\OBSE\\Plugins\\NoPayFineTeleport.log");

		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "NoPayFineTeleport";
		info->version = 2;

		if (obse->isEditor)
			return false;

		if (obse->obseVersion < OBSE_VERSION_INTEGER) {
			_ERROR("OBSE too old (got %08X, need %08X)", obse->obseVersion, OBSE_VERSION_INTEGER);
			return false;
		}
		if (obse->oblivionVersion != OBLIVION_VERSION) {
			_ERROR("unsupported Oblivion version %08X", obse->oblivionVersion);
			return false;
		}

		return true;
	}

	__declspec(dllexport) bool OBSEPlugin_Load(const OBSEInterface* obse)
	{
		g_pluginHandle = obse->GetPluginHandle();
		g_cmdTable = (OBSECommandTableInterface*)obse->QueryInterface(kInterface_CommandTable);
		if (!g_cmdTable) {
			_ERROR("command table interface unavailable, plugin inactive");
			return true;   //stay loaded but do nothing
		}

		InstallHook();
		return true;
	}
};
