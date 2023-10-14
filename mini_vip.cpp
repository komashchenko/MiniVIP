/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * ======================================================
 * Mini VIP
 * Written by Phoenix (˙·٠●Феникс●٠·˙) 2023.
 * ======================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 */

#include <stdio.h>
#include "mini_vip.h"
#include "metamod_oslink.h"
#include "utils.hpp"
#include <utlstring.h>
#include <KeyValues.h>
#include "sdk/schemasystem.h"
#include "sdk/CBaseEntity.h"
#include "sdk/CGameRulesProxy.h"
#include "sdk/CBasePlayerPawn.h"
#include "sdk/CCSPlayerController.h"
#include "sdk/CCSPlayer_ItemServices.h"
#include "sdk/CSmokeGrenadeProjectile.h"
#include <map>

MiniVIP g_MiniVIP;
PLUGIN_EXPOSE(MiniVIP, g_MiniVIP);
IVEngineServer2* engine = nullptr;
IGameEventManager2* gameeventmanager = nullptr;
IGameResourceServiceServer* g_pGameResourceService = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CSchemaSystem* g_pCSchemaSystem = nullptr;
CCSGameRules* g_pGameRules = nullptr;
CPlayerSpawnEvent g_PlayerSpawnEvent;
CRoundPreStartEvent g_RoundPreStartEvent;
CEntityListener g_EntityListener;
bool g_bPistolRound;
std::map<uint32, VipPlayer> g_VipPlayers;

class GameSessionConfiguration_t { };
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);

bool MiniVIP::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceService, IGameResourceServiceServer, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	// Get CSchemaSystem
	{
		HINSTANCE m_hModule = dlmount(WIN_LINUX("schemasystem.dll", "libschemasystem.so"));
		g_pCSchemaSystem = reinterpret_cast<CSchemaSystem*>(reinterpret_cast<CreateInterfaceFn>(dlsym(m_hModule, "CreateInterface"))(SCHEMASYSTEM_INTERFACE_VERSION, nullptr));
		dlclose(m_hModule);
	}

	if (!g_MiniVIP.LoadVips(error, maxlen))
	{
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		
		return false;
	}

	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &MiniVIP::StartupServer), true);
	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &MiniVIP::GameFrame), true);

	gameeventmanager = static_cast<IGameEventManager2*>(CallVFunc<IToolGameEventAPI*, 91>(g_pSource2Server));

	ConVar_Register(FCVAR_GAMEDLL);

	return true;
}

bool MiniVIP::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &MiniVIP::GameFrame), true);
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &MiniVIP::StartupServer), true);

	gameeventmanager->RemoveListener(&g_PlayerSpawnEvent);
	gameeventmanager->RemoveListener(&g_RoundPreStartEvent);

	g_pGameEntitySystem->RemoveListenerEntity(&g_EntityListener);

	ConVar_Unregister();
	
	return true;
}

bool MiniVIP::LoadVips(char* error, size_t maxlen)
{
	KeyValues* pKVConfig = new KeyValues("MiniVIP");
	KeyValues::AutoDelete autoDelete(pKVConfig);
	
	if (!pKVConfig->LoadFromFile(g_pFullFileSystem, "addons/mini_vip/mini_vip.ini"))
	{
		V_strncpy(error, "Failed to load vip config 'addons/mini_vip/mini_vip.ini'", maxlen);
		return false;
	}

	for (KeyValues* pKey = pKVConfig->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey())
	{
		uint32 accontId = V_StringToUint32(pKey->GetName(), 0);
		if (accontId == 0)
		{
			Warning("[%s] accontid is 0\n", GetLogTag());

			continue;
		}

		VipPlayer& player = g_VipPlayers[accontId];
		player.m_iHealth = pKey->GetInt("health", -1);
		player.m_iArmor = pKey->GetInt("armor", -1);
		player.m_fGravity = pKey->GetFloat("gravity", 1.f);
		player.m_iMoneyMin = pKey->GetInt("money_min", -1);
		player.m_iMoneyAdd = pKey->GetInt("money_add", -1);
		player.m_bDefuser = pKey->GetBool("defuser", false);
		if (const char* pszItems = pKey->GetString("items"))
		{
			V_SplitString(pszItems, " ", player.m_items);
		}
		if (!pKey->IsEmpty("smoke_color"))
		{
			player.m_vSmokeColor = new Vector();
			
			const char* pszSmokeColor = pKey->GetString("smoke_color");
			if (strcmp(pszSmokeColor, "random") == 0)
			{
				player.m_vSmokeColor->Invalidate();
			}
			else
			{
				Vector& vSmokeColor = *player.m_vSmokeColor;
				if (sscanf(pszSmokeColor, "%f %f %f", &vSmokeColor.x, &vSmokeColor.y, &vSmokeColor.z) != 3)
				{
					Warning("[%s] %u incorrect smoke_color value is specified (%s), must be r g b or random\n", GetLogTag(), accontId, pszSmokeColor);

					delete player.m_vSmokeColor;
				}
			}
		}
	}

	return true;
}

void MiniVIP::NextFrame(std::function<void()> fn)
{
	m_nextFrame.push_back(fn);
}

void MiniVIP::StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*)
{
	g_pGameRules = nullptr;

	static bool bDone = false;
	if (!bDone)
	{
		g_pGameEntitySystem = *reinterpret_cast<CGameEntitySystem**>(reinterpret_cast<uintptr_t>(g_pGameResourceService) + WIN_LINUX(0x58, 0x50));
		g_pEntitySystem = g_pGameEntitySystem;

		g_pGameEntitySystem->AddListenerEntity(&g_EntityListener);

		gameeventmanager->AddListener(&g_PlayerSpawnEvent, "player_spawn", true);
		gameeventmanager->AddListener(&g_RoundPreStartEvent, "round_prestart", true);

		bDone = true;
	}
}

void MiniVIP::GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	if (!g_pGameRules)
	{
		CCSGameRulesProxy* pGameRulesProxy = static_cast<CCSGameRulesProxy*>(UTIL_FindEntityByClassname(nullptr, "cs_gamerules"));
		if (pGameRulesProxy)
		{
			g_pGameRules = pGameRulesProxy->m_pGameRules();
		}
	}
	
	while (!m_nextFrame.empty())
	{
		m_nextFrame.front()();
		m_nextFrame.pop_front();
	}
}

void CPlayerSpawnEvent::FireGameEvent(IGameEvent* event)
{
	if (!g_pGameRules || g_pGameRules->m_bWarmupPeriod())
		return;
	
	CBasePlayerController* pPlayerController = static_cast<CBasePlayerController*>(event->GetPlayerController("userid"));
	if (!pPlayerController || pPlayerController->m_steamID() == 0) // Ignore bots
		return;

	g_MiniVIP.NextFrame([hPlayerController = CHandle<CBasePlayerController>(pPlayerController)]()
	{
		CCSPlayerController* pPlayerController = static_cast<CCSPlayerController*>(hPlayerController.Get());
		if (!pPlayerController)
			return;

		CCSPlayerPawnBase* pPlayerPawn = pPlayerController->m_hPlayerPawn();
		if (!pPlayerPawn || pPlayerPawn->m_lifeState() != LIFE_ALIVE)
			return;

		auto vipPlayer = g_VipPlayers.find(static_cast<uint32>(pPlayerController->m_steamID()));
		if (vipPlayer == g_VipPlayers.end() || !engine->IsClientFullyAuthenticated(pPlayerController->m_pEntity->m_EHandle.GetEntryIndex() - 1))
			return;

		CCSPlayer_ItemServices* pItemServices = static_cast<CCSPlayer_ItemServices*>(pPlayerPawn->m_pItemServices());
		VipPlayer& data = vipPlayer->second;

		if (!g_bPistolRound)
		{
			CCSPlayerController_InGameMoneyServices* pMoneyServices = pPlayerController->m_pInGameMoneyServices();

			if (data.m_iMoneyAdd != -1)
				pMoneyServices->m_iAccount() += data.m_iMoneyAdd;

			if (data.m_iMoneyMin != -1)
			{
				if (pMoneyServices->m_iAccount() < data.m_iMoneyMin)
					pMoneyServices->m_iAccount() = data.m_iMoneyMin;
			}

			for (int i = 0; i < data.m_items.Count(); i++)
			{
				pItemServices->GiveNamedItem(data.m_items[i]);
			}
		}

		if (data.m_iHealth != -1)
		{
			pPlayerPawn->m_iMaxHealth() = data.m_iHealth;
			pPlayerPawn->m_iHealth() = data.m_iHealth;
		}

		if (data.m_iArmor != -1)
		{
			pPlayerPawn->m_ArmorValue() = data.m_iArmor;
			pItemServices->m_bHasHelmet() = true;
		}

		if (data.m_bDefuser && pPlayerPawn->m_iTeamNum() == 3)
		{
			pItemServices->m_bHasDefuser() = true;
		}

		if (data.m_fGravity != 1.f)
		{
			pPlayerPawn->m_flGravityScale() = data.m_fGravity;
		}
	});
}

void CRoundPreStartEvent::FireGameEvent(IGameEvent* event)
{
	if (g_pGameRules)
	{
		g_bPistolRound = g_pGameRules->m_totalRoundsPlayed() == 0 || (g_pGameRules->m_bSwitchingTeamsAtRoundReset() && g_pGameRules->m_nOvertimePlaying() == 0) || g_pGameRules->m_bGameRestart();
	}
}

void CEntityListener::OnEntitySpawned(CEntityInstance* pEntity)
{
	CSmokeGrenadeProjectile* pGrenadeProjectile = dynamic_cast<CSmokeGrenadeProjectile*>(pEntity);
	if (!pGrenadeProjectile)
		return;

	g_MiniVIP.NextFrame([hGrenadeProjectile = CHandle<CSmokeGrenadeProjectile>(pGrenadeProjectile)]()
	{
		CSmokeGrenadeProjectile* pGrenadeProjectile = hGrenadeProjectile;
		if (!pGrenadeProjectile)
			return;

		CCSPlayerPawn* pPlayerPawn = pGrenadeProjectile->m_hThrower();
		if (!pPlayerPawn)
			return;

		CBasePlayerController* pPlayerController = pPlayerPawn->m_hController();
		if (!pPlayerController || pPlayerController->m_steamID() == 0)
			return;

		auto vipPlayer = g_VipPlayers.find(static_cast<uint32>(pPlayerController->m_steamID()));
		if (vipPlayer == g_VipPlayers.end())
			return;

		Vector* pvSmokeColor = vipPlayer->second.m_vSmokeColor;
		if (!pvSmokeColor)
			return;

		pGrenadeProjectile->m_vSmokeColor() = pvSmokeColor->IsValid() ? *pvSmokeColor : Vector(rand() % 255, rand() % 255, rand() % 255);
	});
}

CON_COMMAND_F(mini_vip_reload, "reloads list of vip players", FCVAR_NONE)
{
	g_VipPlayers.clear();
	
	char szError[256];
	if (g_MiniVIP.LoadVips(szError, sizeof(szError)))
	{
		ConColorMsg({ 0, 255, 0, 255 }, "VIP players has been successfully updated\n");
	}
	else
	{
		ConColorMsg({ 255, 0, 0, 255 }, "Reload error: %s\n", szError);
	}
}

///////////////////////////////////////
const char* MiniVIP::GetLicense()
{
	return "GPL";
}

const char* MiniVIP::GetVersion()
{
	return "1.0.0";
}

const char* MiniVIP::GetDate()
{
	return __DATE__;
}

const char *MiniVIP::GetLogTag()
{
	return "MiniVIP";
}

const char* MiniVIP::GetAuthor()
{
	return "Phoenix (˙·٠●Феникс●٠·˙)";
}

const char* MiniVIP::GetDescription()
{
	return "Mini VIP system";
}

const char* MiniVIP::GetName()
{
	return "Mini VIP";
}

const char* MiniVIP::GetURL()
{
	return "https://github.com/komashchenko/MiniVIP";
}
