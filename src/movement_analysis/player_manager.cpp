#include "player_context.h"

#include "tier0/memdbgon.h"
AntiCheatPlayerManager g_AntiCheatPlayerManager;

AntiCheatPlayerManager *g_pAntiCheatPlayerManager = &g_AntiCheatPlayerManager;
PlayerManager *g_pPlayerManager = dynamic_cast<PlayerManager *>(&g_AntiCheatPlayerManager);

AntiCheatPlayerManager::AntiCheatPlayerManager()
{
	for (i32 i = 0; i <= MAXPLAYERS; ++i)
	{
		delete players[i];
		players[i] = new AntiCheatPlayer(i);
	}
}

AntiCheatPlayer *AntiCheatPlayerManager::ToPlayer(CPlayerPawnComponent *component)
{
	return static_cast<AntiCheatPlayer *>(MovementPlayerManager::ToPlayer(component));
}

AntiCheatPlayer *AntiCheatPlayerManager::ToPlayer(CBasePlayerController *controller)
{
	return static_cast<AntiCheatPlayer *>(MovementPlayerManager::ToPlayer(controller));
}

AntiCheatPlayer *AntiCheatPlayerManager::ToPlayer(CBasePlayerPawn *pawn)
{
	return static_cast<AntiCheatPlayer *>(MovementPlayerManager::ToPlayer(pawn));
}

AntiCheatPlayer *AntiCheatPlayerManager::ToPlayer(CPlayerSlot slot)
{
	return static_cast<AntiCheatPlayer *>(MovementPlayerManager::ToPlayer(slot));
}

AntiCheatPlayer *AntiCheatPlayerManager::ToPlayer(CEntityIndex entIndex)
{
	return static_cast<AntiCheatPlayer *>(MovementPlayerManager::ToPlayer(entIndex));
}

AntiCheatPlayer *AntiCheatPlayerManager::ToPlayer(CPlayerUserId userID)
{
	return static_cast<AntiCheatPlayer *>(MovementPlayerManager::ToPlayer(userID));
}

AntiCheatPlayer *AntiCheatPlayerManager::ToPlayer(u32 index)
{
	return static_cast<AntiCheatPlayer *>(MovementPlayerManager::players[index]);
}

AntiCheatPlayer *AntiCheatPlayerManager::SteamIdToPlayer(u64 steamID, bool validated)
{
	return static_cast<AntiCheatPlayer *>(PlayerManager::SteamIdToPlayer(steamID, validated));
}
