#include "AntiCheat.h"

#include "localization.h"
#include "movement_analysis/detection/movement_detection.h"
#include "movement_analysis/player_context.h"
#include "movement_analysis/settings/movement_settings.h"
#include "sdk/cgameresourceserviceserver.h"
#include "sdk/navphysicsinterface.h"
#include "settings.h"
#include "webhook.h"
#include "utils/addresses.h"
#include "utils/ctimer.h"
#include "utils/detours.h"
#include "utils/gameconfig.h"
#include "utils/hooks.h"
#include "utils/interfaces.h"
#include "utils/schema.h"
#include "utils/utils.h"
#include "networksystem/inetworkmessages.h"
#include "cs_gameevents.pb.h"
#include "gameevents.pb.h"
#include "igameevents.h"

AntiCheatPlugin g_AntiCheat;
IClientCvarValue *g_pClientCvarValue {};

PLUGIN_EXPOSE(AntiCheatPlugin, g_AntiCheat);

namespace
{
	static constexpr const char *detectionNames[] = {
		"AIMBOT",    "AIMLOCK",     "ANTIAIM",          "AUTOSTRAFE",   "BHOP",          "DLL INJECTION",      "DESUBTICKING",
		"DOUBLETAP", "HYPERSCROLL", "INHUMAN ACCURACY", "INVALID CVAR", "INVALID INPUT", "IRREGULAR BEHAVIOR", "NAMECHANGER",
		"NULLS",     "SILENTAIM",   "SUBTICK SPAM",     "TRIGGERBOT",
	};
	static_assert(AntiCheat_ARRAYSIZE(detectionNames) == static_cast<size_t>(DetectionType::Count));

	void HandleDetectionCallback(const char *detection, MovementPlayer *player, const localization::Text &evidence)
	{
		g_AntiCheat.HandleDetection(detection, player, evidence);
	}

	void HandleNetworkVetoDetectionCallback(const char *detection, MovementPlayer *player, const localization::Text &evidence)
	{
		g_AntiCheat.HandleDetection(detection, player, evidence, false, true);
	}

	bool IsKickOnlyDetection(const char *detection)
	{
		return AntiCheat_STREQI(detection, "DESUBTICKING") || AntiCheat_STREQI(detection, "NULLS") || AntiCheat_STREQI(detection, "SUBTICK SPAM");
	}

	void ReplaceAll(std::string &value, const std::string &placeholder, const std::string &replacement)
	{
		for (size_t position = 0; (position = value.find(placeholder, position)) != std::string::npos; position += replacement.size())
		{
			value.replace(position, placeholder.size(), replacement);
		}
	}

	std::string SanitizeConsoleText(const char *value)
	{
		std::string safe = value ? value : "<unknown>";
		for (char &character : safe)
		{
			const auto byte = static_cast<unsigned char>(character);
			if (byte < 32 || byte == 127)
			{
				character = '?';
			}
		}
		return safe;
	}

	int CheckCommandTemplate(const char *settingName, const char *commandTemplate, bool requireSteamId)
	{
		if (!commandTemplate || !*commandTemplate)
		{
			Msg("[AntiCheat] Review %s: it is empty, so this punishment is disabled.\n", settingName);
			return 1;
		}

		int findings = 0;
		bool hasSteamId = false;
		bool hasUserId = false;
		const std::string command = commandTemplate;
		if (command.front() == '"' || command.back() == '"')
		{
			Msg("[AntiCheat] Review %s: the command itself starts or ends with a quote. Keep only the cfg value's outer quotes.\n", settingName);
			++findings;
		}
		for (size_t position = 0; position < command.size();)
		{
			const size_t open = command.find('{', position);
			const size_t close = command.find('}', position);
			if (close != std::string::npos && (open == std::string::npos || close < open))
			{
				Msg("[AntiCheat] Review %s: it contains an unmatched closing brace.\n", settingName);
				++findings;
				position = close + 1;
				continue;
			}
			if (open == std::string::npos)
			{
				break;
			}
			const size_t placeholderEnd = command.find('}', open + 1);
			if (placeholderEnd == std::string::npos)
			{
				Msg("[AntiCheat] Review %s: it contains an unmatched opening brace.\n", settingName);
				++findings;
				break;
			}

			const std::string placeholder = command.substr(open, placeholderEnd - open + 1);
			hasSteamId |= placeholder == "{steamid64}";
			hasUserId |= placeholder == "{userid}";
			if (placeholder != "{steamid64}" && placeholder != "{userid}" && placeholder != "{detection}")
			{
				Msg("[AntiCheat] Review %s: %s is not a supported placeholder.\n", settingName, SanitizeConsoleText(placeholder.c_str()).c_str());
				++findings;
			}
			position = placeholderEnd + 1;
		}

		if (requireSteamId && !hasSteamId)
		{
			Msg("[AntiCheat] Review %s: permanent bans need the {steamid64} placeholder.\n", settingName);
			++findings;
		}
		if (!requireSteamId && !hasSteamId && !hasUserId)
		{
			Msg("[AntiCheat] Review %s: kicks need either {userid} or {steamid64}.\n", settingName);
			++findings;
		}

		std::string expanded = command;
		ReplaceAll(expanded, "{steamid64}", "18446744073709551615");
		ReplaceAll(expanded, "{userid}", "2147483647");
		ReplaceAll(expanded, "{detection}", "IRREGULAR BEHAVIOR");
		if (expanded.size() >= static_cast<size_t>(CCommand::MaxCommandLength()))
		{
			Msg("[AntiCheat] Review %s: its expanded command can exceed the engine limit.\n", settingName);
			++findings;
		}
		return findings;
	}

	f64 ConfigLoadTimeout()
	{
		g_AntiCheat.ReportConfigLoadTimeout();
		return 0.0;
	}

	void AddOffsetRequirement(const char *key, const char *plainName, std::vector<std::string> &missing)
	{
		if (!g_pGameConfig || g_pGameConfig->GetOffset(key) < 0)
		{
			missing.emplace_back(std::string("The ") + plainName + " offset is missing from the AntiCheat game data.");
		}
	}

	struct SchemaRequirement
	{
		const char *className;
		const char *fieldName;
	};

	void AddSchemaRequirements(std::vector<std::string> &missing)
	{
		if (!g_pSchemaSystem || !modules::schemasystem || !modules::schemasystem->m_hModule || !modules::schemasystem->m_base
			|| !modules::schemasystem->m_size)
		{
			return;
		}
		static constexpr SchemaRequirement requirements[] = {
			{"CBaseEntity", "m_CBodyComponent"},
			{"CBaseEntity", "m_fFlags"},
			{"CBaseEntity", "m_hGroundEntity"},
			{"CBaseEntity", "m_flActualGravityScale"},
			{"CBaseEntity", "m_flGravityScale"},
			{"CBaseEntity", "m_flWaterLevel"},
			{"CBaseEntity", "m_hOwnerEntity"},
			{"CBaseEntity", "m_iTeamNum"},
			{"CBaseEntity", "m_lifeState"},
			{"CBaseEntity", "m_MoveType"},
			{"CBaseEntity", "m_nActualMoveType"},
			{"CBaseEntity", "m_nSubclassID"},
			{"CBaseEntity", "m_pCollision"},
			{"CBaseEntity", "m_vecAbsVelocity"},
			{"CBaseEntity", "m_vecBaseVelocity"},
			{"CBaseModelEntity", "m_Collision"},
			{"CBaseModelEntity", "m_vecViewOffset"},
			{"CBasePlayerController", "m_bIsHLTV"},
			{"CBasePlayerController", "m_iszPlayerName"},
			{"CBasePlayerController", "m_steamID"},
			{"CBasePlayerPawn", "m_hController"},
			{"CBasePlayerPawn", "m_pMovementServices"},
			{"CBasePlayerPawn", "m_pWeaponServices"},
			{"CBasePlayerPawn", "v_angle"},
			{"CBodyComponent", "m_pSceneNode"},
			{"CCollisionProperty", "m_collisionAttribute"},
			{"CCollisionProperty", "m_CollisionGroup"},
			{"CCSPlayer_MovementServices", "m_bDucked"},
			{"CCSPlayer_MovementServices", "m_ModernJump"},
			{"CCSPlayer_MovementServices", "m_vecLadderNormal"},
			{"CCSPlayerController", "m_hPlayerPawn"},
			{"CCSPlayerModernJump", "m_flLastLandedFrac"},
			{"CCSPlayerModernJump", "m_flLastUsableJumpPressFrac"},
			{"CCSPlayerModernJump", "m_nLastLandedTick"},
			{"CCSPlayerModernJump", "m_nLastUsableJumpPressTick"},
			{"CCSPlayerPawn", "m_angEyeAngles"},
			{"CCSPlayerPawn", "m_bOnGroundLastTick"},
			{"CCSPlayerPawn", "m_bIsScoped"},
			{"CCSPlayerPawn", "m_entitySpottedState"},
			{"CCSPlayerPawn", "m_ignoreLadderJumpTime"},
			{"CCSWeaponBaseVData", "m_flCycleTime"},
			{"CGameSceneNode", "m_vecAbsOrigin"},
			{"EntitySpottedState_t", "m_bSpottedByMask"},
			{"CPlayer_MovementServices", "m_nButtons"},
			{"CPlayer_MovementServices", "m_vecLastMovementImpulses"},
			{"CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"},
			{"CPlayer_WeaponServices", "m_hActiveWeapon"},
			{"VPhysicsCollisionAttribute_t", "m_nHierarchyId"},
			{"VPhysicsCollisionAttribute_t", "m_nInteractsWith"},
		};
		for (const auto &requirement : requirements)
		{
			if (!schema::HasField(requirement.className, requirement.fieldName))
			{
				missing.emplace_back(std::string("The required player data field ") + requirement.className + "." + requirement.fieldName
									 + " is unavailable.");
			}
		}
	}

	std::string JoinReasons(const std::vector<std::string> &missing)
	{
		std::string message = "[AntiCheat] AntiCheat did not load.";
		for (const auto &reason : missing)
		{
			message += " ";
			message += reason;
		}
		return message;
	}
} // namespace

CON_COMMAND(AntiCheat_status, "Show the current AntiCheat status")
{
	(void)args;
	g_AntiCheat.PrintStatus();
}

CON_COMMAND(AntiCheat_help, "Show the available AntiCheat administrator commands")
{
	(void)args;
	g_AntiCheat.PrintHelp();
}

CON_COMMAND(AntiCheat_reload, "Reload and validate AntiCheat.cfg")
{
	(void)args;
	if (!g_AntiCheat.IsLoaded())
	{
		Msg("[AntiCheat] AntiCheat is not running. Run AntiCheat_status for the activation error.\n");
		return;
	}
	g_AntiCheat.ReloadConfig();
}

CON_COMMAND(AntiCheat_check_config, "Check the current AntiCheat settings without changing them")
{
	(void)args;
	if (!g_AntiCheat.IsLoaded())
	{
		Msg("[AntiCheat] AntiCheat is not running. Run AntiCheat_status for the activation error.\n");
		return;
	}
	g_AntiCheat.CheckConfig();
}

CON_COMMAND(AntiCheat_test_announcement, "Show a harmless AntiCheat chat and center-screen test")
{
	(void)args;
	if (!g_AntiCheat.IsLoaded())
	{
		Msg("[AntiCheat] AntiCheat is not running. Run AntiCheat_status for the activation error.\n");
		return;
	}
	g_AntiCheat.TestAnnouncement();
}

CON_COMMAND(AntiCheat_webhook_test, "Send a harmless Discord webhook test")
{
	(void)args;
	if (!g_AntiCheat.IsLoaded())
	{
		Msg("[AntiCheat] AntiCheat is not running. Run AntiCheat_status for the activation error.\n");
		return;
	}
	g_AntiCheat.TestWebhook();
}

CON_COMMAND(AntiCheat_config_loaded, "Confirm that AntiCheat.cfg finished loading")
{
	(void)args;
	if (!g_AntiCheat.IsLoaded())
	{
		return;
	}
	g_AntiCheat.OnConfigLoaded();
}

bool AntiCheatPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	if (!settings::Initialize())
	{
		if (error && maxlen)
		{
			snprintf(error, maxlen, "AntiCheat could not reserve memory for its settings.");
		}
		return false;
	}
	webhook = new (std::nothrow) WebhookService;
	if (!webhook)
	{
		settings::Shutdown();
		if (error && maxlen)
		{
			snprintf(error, maxlen, "AntiCheat could not reserve memory for Discord reports.");
		}
		return false;
	}
	if (late)
	{
		if (!Activate(error, maxlen, true))
		{
			CleanupRuntime();
			return false;
		}
		ismm->AddListener(this, this);
	}
	else
	{
		// The game DLL finishes registering its messages and events after this early callback.
		activationPending = true;
	}
	return true;
}

void AntiCheatPlugin::AllPluginsLoaded()
{
	if (!activationPending)
	{
		return;
	}

	activationPending = false;
	char error[1024] {};
	if (!Activate(error, sizeof(error), false))
	{
		activationError = error[0] ? error : "AntiCheat could not finish loading after the game became ready.";
		Msg("[AntiCheat] AntiCheat is not running. %s\n", activationError.c_str());
		return;
	}
	g_SMAPI->AddListener(this, this);
}

bool AntiCheatPlugin::QueryRunning(char *error, size_t maxlen)
{
	if (loaded)
	{
		return true;
	}
	const char *reason = activationError.empty() ? "AntiCheat is waiting for the game to finish starting." : activationError.c_str();
	if (error && maxlen)
	{
		snprintf(error, maxlen, "%s", reason);
	}
	return false;
}

bool AntiCheatPlugin::Activate(char *error, size_t maxlen, bool late)
{
	std::vector<std::string> missing;
	if (!KHook::__exported__khook)
	{
		missing.emplace_back("Metamod's KHook service is unavailable.");
	}

	modules::Initialize(missing);
	interfaces::Initialize(g_SMAPI, missing);
	utils::Initialize(missing);

	if (g_pCVar && !convarsRegistered)
	{
		META_CONVAR_REGISTER(FCVAR_RELEASE);
		convarsRegistered = true;
	}

	if (g_pGameConfig)
	{
		AddOffsetRequirement("Teleport", "player teleport", missing);
		AddOffsetRequirement("IsEntityPawn", "player pawn check", missing);
		AddOffsetRequirement("IsEntityController", "player controller check", missing);
		AddOffsetRequirement("ClientOffset", "player list", missing);
	}

	if (g_pNetworkMessages && !g_pNetworkMessages->FindNetworkMessagePartial("TextMsg"))
	{
		missing.emplace_back("The chat message used for detection announcements is unavailable.");
	}
	if (g_pNetworkMessages && !g_pNetworkMessages->FindNetworkMessageById(GE_Source1LegacyGameEvent))
	{
		missing.emplace_back("The center-screen event message used for detection announcements is unavailable.");
	}
	if (g_pNetworkMessages && !g_pNetworkMessages->FindNetworkMessageById(GE_FireBulletsId))
	{
		missing.emplace_back("The exact weapon firing data used by Silentaim is unavailable.");
	}
	if (g_pNetworkMessages && !g_pNetworkMessages->FindNetworkMessageById(GE_PlayerBulletHitId))
	{
		missing.emplace_back("The exact bullet-hit context used by Aimbot is unavailable.");
	}
	if (g_pCVar)
	{
		movement_settings::Validate(missing);
		ConVarRefAbstract svCheats("sv_cheats", true);
		if (!svCheats.IsValidRef() || !svCheats.IsConVarDataAvailable())
		{
			missing.emplace_back("The server variable sv_cheats is not available.");
		}
	}
	if (modules::server && modules::server->m_hModule && !modules::server->FindVirtualTable("CNavPhysicsInterface"))
	{
		missing.emplace_back("The movement trace interface could not be found.");
	}
	if (modules::server && modules::server->m_hModule && !modules::server->FindVirtualTable("CTraceFilterPlayerMovementCS"))
	{
		missing.emplace_back("The player movement trace filter could not be found.");
	}
	AddSchemaRequirements(missing);
	movement::ValidateDetours(missing);

	char clientCvarError[512] {};
	if (!g_ClientCvarValue.Validate(interfaces::pEngine, g_pNetworkMessages, clientCvarError, sizeof(clientCvarError)))
	{
		missing.emplace_back(clientCvarError[0] ? clientCvarError : "Player setting checks are unavailable.");
	}

	if (!missing.empty())
	{
		const std::string message = JoinReasons(missing);
		for (const auto &reason : missing)
		{
			Msg("[AntiCheat] %s\n", reason.c_str());
		}
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", message.c_str());
		}
		CleanupRuntime(false);
		return false;
	}

	bool clientCvarsReady =
		g_ClientCvarValue.Load(interfaces::pEngine, g_pNetworkMessages, interfaces::pGameEventSystem, clientCvarError, sizeof(clientCvarError));
	if (!clientCvarsReady)
	{
		missing.emplace_back(clientCvarError[0] ? clientCvarError : "Player setting checks could not be started.");
	}
	else
	{
		g_pClientCvarValue = g_ClientCvarValue.GetInterface();
	}
	if (!clientCvarsReady)
	{
		const std::string message = JoinReasons(missing);
		for (const auto &reason : missing)
		{
			Msg("[AntiCheat] %s\n", reason.c_str());
		}
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", message.c_str());
		}
		CleanupRuntime(false);
		return false;
	}

	MovementDetectionService::InitSvCheatsWatcher();
	svCheatsWatcherInstalled = true;
	detectionSystem.Load(HandleDetectionCallback, HandleNetworkVetoDetectionCallback);
	hooks::Initialize(missing);

	if (!missing.empty())
	{
		const std::string message = JoinReasons(missing);
		for (const auto &reason : missing)
		{
			Msg("[AntiCheat] %s\n", reason.c_str());
		}
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", message.c_str());
		}
		CleanupRuntime(false);
		return false;
	}

	// Install raw detours last so no later startup failure can leave one behind.
	if (!movement::InitDetours(missing))
	{
		const std::string message = JoinReasons(missing);
		for (const auto &reason : missing)
		{
			Msg("[AntiCheat] %s\n", reason.c_str());
		}
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", message.c_str());
		}
		CleanupRuntime(false);
		return false;
	}

	loaded = true;
	ResetRuntime();
	configReloadPending = true;
	configLoadFailed = false;
	settings::ExecuteConfig();
	StartTimer(ConfigLoadTimeout, 5.0, true, true);
	if (late)
	{
		for (i32 i = 1; i <= MAXPLAYERS; ++i)
		{
			auto *player = g_pAntiCheatPlayerManager->ToPlayer(static_cast<u32>(i));
			if (player && player->IsInGame())
			{
				g_pAntiCheatPlayerManager->OnClientActive(player->GetPlayerSlot(), player->GetSteamId64(false));
				g_pAntiCheatPlayerManager->OnClientFullyConnect(player->GetPlayerSlot());
				g_ClientCvarValue.OnClientFullyConnected(player->GetPlayerSlot(), player->IsFakeClient());
				OnClientFullyConnect(player->GetPlayerSlot());
			}
		}
		hooks::HookActivePlayers();
	}
	Msg("[AntiCheat] AntiCheat %s loaded successfully. Waiting for AntiCheat.cfg to finish.\n", PLUGIN_FULL_VERSION);
	return true;
}

bool AntiCheatPlugin::Unload(char *error, size_t maxlen)
{
	if (!FlushAllDetours())
	{
		const char *reason = "AntiCheat could not unload because one of its server hooks could not be removed safely.";
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", reason);
		}
		Msg("[AntiCheat] %s\n", reason);
		return false;
	}
	CleanupRuntime();
	Msg("[AntiCheat] AntiCheat unloaded successfully.\n");
	return true;
}

bool AntiCheatPlugin::Pause(char *error, size_t maxlen)
{
	const char *reason = "AntiCheat cannot be paused safely. Unload it instead.";
	if (error && maxlen)
	{
		snprintf(error, maxlen, "%s", reason);
	}
	return false;
}

bool AntiCheatPlugin::Unpause(char *error, size_t maxlen)
{
	const char *reason = "AntiCheat is not paused.";
	if (error && maxlen)
	{
		snprintf(error, maxlen, "%s", reason);
	}
	return false;
}

void AntiCheatPlugin::OnLevelInit(char const *, char const *, char const *, char const *, bool, bool)
{
	if (loaded)
	{
		ResetRuntime();
		Msg("[AntiCheat] A new map is ready. Detection evidence started fresh.\n");
	}
}

void AntiCheatPlugin::OnLevelShutdown()
{
	if (loaded)
	{
		ResetRuntime();
		Msg("[AntiCheat] The map ended. Detection evidence was cleared.\n");
	}
}

void AntiCheatPlugin::OnProcessUsercmds(MovementPlayer *player, PlayerCommand *commands, int numCommands)
{
	detectionSystem.OnProcessUsercmds(player, commands, numCommands);
}

void AntiCheatPlugin::OnSetupMove(MovementPlayer *player, PlayerCommand *command)
{
	auto *globals = g_pAntiCheatUtils->GetServerGlobals();
	detectionSystem.OnSetupMove(player, command, globals ? globals->tickcount : 0);
}

void AntiCheatPlugin::OnGameFrame(bool simulating)
{
	auto *globals = g_pAntiCheatUtils->GetServerGlobals();
	if (simulating)
	{
		detectionSystem.OnGameFrame(globals ? globals->tickcount : 0);
	}
	if (webhook)
	{
		webhook->OnGameFrame();
	}
	ProcessJoinWatermarks();
}

void AntiCheatPlugin::OnGameEvent(IGameEvent *event, MovementPlayer *player)
{
	auto *globals = g_pAntiCheatUtils->GetServerGlobals();
	detectionSystem.OnGameEvent(event, player, globals ? globals->tickcount : 0);
}

void AntiCheatPlugin::OnFireBullets(const CMsgTEFireBullets &event)
{
	auto *globals = g_pAntiCheatUtils->GetServerGlobals();
	detectionSystem.OnFireBullets(event, globals ? globals->tickcount : 0);
}

void AntiCheatPlugin::OnPlayerBulletHit(const CMsgPlayerBulletHit &event)
{
	auto *globals = g_pAntiCheatUtils->GetServerGlobals();
	detectionSystem.OnPlayerBulletHit(event, globals ? globals->tickcount : 0);
}

void AntiCheatPlugin::HandleDetection(const char *detection, MovementPlayer *player, const localization::Text &evidence, bool kickOnly,
								  bool networkVetoed)
{
	if (!detection || !*detection || !player || player->index < 1 || player->index > MAXPLAYERS)
	{
		return;
	}

	const std::string playerName = SanitizeConsoleText(player->GetName());
	const std::uint64_t steamId = player->GetSteamId64(false);
	const auto finish = [&](utils::DetectionOutcome outcome)
	{
		utils::AnnounceDetection(detection, player->GetName(), outcome);
		if (webhook)
		{
			webhook->Report(detection, player, evidence.localized, outcome);
		}
	};
	if (steamId)
	{
		Msg("[AntiCheat] Detected %s on %s (SteamID64 %llu).\n", detection, playerName.c_str(), static_cast<unsigned long long>(steamId));
	}
	else
	{
		Msg("[AntiCheat] Detected %s on %s (SteamID64 unavailable).\n", detection, playerName.c_str());
	}
	if (!evidence.english.empty())
	{
		Msg("[AntiCheat] Evidence: %s\n", SanitizeConsoleText(evidence.english.c_str()).c_str());
	}
	if (networkVetoed)
	{
		finish(utils::DetectionOutcome::NetworkUnstable);
		Msg("[AntiCheat] No punishment was sent because %s's connection exceeded the safe network limits.\n", playerName.c_str());
		return;
	}
	if (!steamId)
	{
		finish(utils::DetectionOutcome::IdentityUnavailable);
		Msg("[AntiCheat] No punishment was sent because %s's SteamID64 is not ready yet.\n", playerName.c_str());
		return;
	}

	const bool whitelisted = settings::IsPlayerWhitelisted(steamId);
	if (whitelisted)
	{
		finish(utils::DetectionOutcome::Whitelisted);
		Msg("[AntiCheat] No punishment was sent because %s is whitelisted.\n", playerName.c_str());
		return;
	}

	const PunishmentLevel requested = kickOnly || IsKickOnlyDetection(detection) ? PunishmentLevel::Kick : PunishmentLevel::Ban;
	auto &issued = punishmentLevels[player->index];
	if (issued >= requested)
	{
		finish(utils::DetectionOutcome::AlreadyPunished);
		Msg("[AntiCheat] No duplicate punishment was sent for %s.\n", playerName.c_str());
		return;
	}

	const char *commandTemplate = requested == PunishmentLevel::Kick ? settings::GetKickCommand() : settings::GetPunishmentCommand();
	if (!commandTemplate || !*commandTemplate)
	{
		finish(utils::DetectionOutcome::PunishmentDisabled);
		Msg("[AntiCheat] No punishment was sent because the matching command is empty.\n");
		return;
	}
	if (!interfaces::pEngine)
	{
		finish(utils::DetectionOutcome::CommandServiceUnavailable);
		Msg("[AntiCheat] No punishment was sent because the server command service is unavailable.\n");
		return;
	}

	const int userId = interfaces::pEngine->GetPlayerUserId(player->GetPlayerSlot()).Get();
	std::string command = commandTemplate;
	if (userId < 0 && command.find("{userid}") != std::string::npos)
	{
		finish(utils::DetectionOutcome::IdentityUnavailable);
		Msg("[AntiCheat] No punishment was sent because %s's user ID is not ready yet.\n", playerName.c_str());
		return;
	}
	ReplaceAll(command, "{steamid64}", std::to_string(steamId));
	ReplaceAll(command, "{userid}", std::to_string(userId));
	ReplaceAll(command, "{detection}", detection);
	if (command.size() >= static_cast<size_t>(CCommand::MaxCommandLength()))
	{
		finish(utils::DetectionOutcome::CommandTooLong);
		Msg("[AntiCheat] No punishment was sent because the configured command is too long.\n");
		return;
	}
	finish(utils::DetectionOutcome::PunishmentSent);
	command.push_back('\n');
	interfaces::pEngine->ServerCommand(command.c_str());
	command.pop_back();
	Msg("[AntiCheat] Sent: %s\n", SanitizeConsoleText(command.c_str()).c_str());
	issued = requested;
}

void AntiCheatPlugin::OnClientFullyConnect(CPlayerSlot slot)
{
	auto *player = g_pAntiCheatPlayerManager->ToPlayer(slot);
	detectionSystem.OnClientReady(player);
	const int index = slot.Get() + 1;
	if (player && index > 0 && index <= MAXPLAYERS && !player->IsFakeClient() && !player->IsCSTV() && !joinWatermarks[index].shown
		&& !joinWatermarks[index].pending)
	{
		joinWatermarks[index].showAt = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		joinWatermarks[index].pending = true;
	}
}

void AntiCheatPlugin::OnClientSettingsChanged(CPlayerSlot slot)
{
	detectionSystem.OnClientSettingsChanged(g_pAntiCheatPlayerManager->ToPlayer(slot));
}

void AntiCheatPlugin::OnClientDisconnect(CPlayerSlot slot)
{
	auto *player = g_pAntiCheatPlayerManager->ToPlayer(slot);
	const std::string playerName = player ? SanitizeConsoleText(player->GetName()) : "<unknown>";
	detectionSystem.OnClientDisconnect(player);
	const int index = slot.Get() + 1;
	if (index > 0 && index <= MAXPLAYERS)
	{
		joinWatermarks[index] = {};
	}
	if (player)
	{
		punishmentLevels[player->index] = PunishmentLevel::None;
		Msg("[AntiCheat] Cleared %s's detection evidence because they disconnected.\n", playerName.c_str());
	}
}

void AntiCheatPlugin::ProcessJoinWatermarks()
{
	const auto now = std::chrono::steady_clock::now();
	for (int index = 1; index <= MAXPLAYERS; ++index)
	{
		auto &state = joinWatermarks[index];
		if (!state.pending && !state.centerPending && !state.centerActive)
		{
			continue;
		}
		auto *player = g_pAntiCheatPlayerManager->ToPlayer(static_cast<u32>(index));
		if (!player || !player->IsConnected() || player->IsFakeClient() || player->IsCSTV())
		{
			state = {};
			continue;
		}
		if (state.pending && now >= state.showAt && player->IsInGame())
		{
			state.pending = false;
			state.shown = true;
			state.centerPending = true;
			utils::AnnounceWatermarkTo(player->GetPlayerSlot(), false);
		}
		if (state.centerPending && player->IsInGame() && !utils::IsDetectionAnnouncementActive())
		{
			state.centerPending = false;
			utils::AnnounceWatermarkTo(player->GetPlayerSlot(), true);
			state.centerActive = true;
			state.expires = now + std::chrono::seconds(3);
			state.nextCenterSend = now + std::chrono::milliseconds(100);
			state.centerBroadcasts = 1;
		}
		if (!state.centerActive)
		{
			continue;
		}
		if (utils::IsDetectionAnnouncementActive() || !player->IsInGame())
		{
			state.centerActive = false;
			continue;
		}
		if (now >= state.expires)
		{
			utils::ClearWatermarkFor(player->GetPlayerSlot());
			state.centerActive = false;
			continue;
		}
		if (now >= state.nextCenterSend && state.centerBroadcasts < 31)
		{
			utils::AnnounceWatermarkTo(player->GetPlayerSlot(), true);
			state.nextCenterSend = now + std::chrono::milliseconds(100);
			++state.centerBroadcasts;
		}
	}
}

void AntiCheatPlugin::PrintConfigSummary(bool reloaded) const
{
	const size_t enabled = settings::GetEnabledDetectionCount();
	const size_t total = static_cast<size_t>(DetectionType::Count);
	Msg("[AntiCheat] Configuration %s: %zu/%zu detectors enabled, %zu whitelisted SteamIDs, %zu rejected entries, %zu duplicates ignored.\n",
		reloaded ? "reloaded" : "loaded", enabled, total, settings::GetWhitelistCount(), settings::GetRejectedWhitelistCount(),
		settings::GetDuplicateWhitelistCount());
	Msg("[AntiCheat] Public announcements: chat %s, center screen %s.\n", settings::ShowChatAnnouncements() ? "on" : "off",
		settings::ShowCenterAnnouncements() ? "on" : "off");
	Msg("[AntiCheat] Punishments: permanent ban %s, kick %s.\n",
		settings::GetPunishmentCommand() && *settings::GetPunishmentCommand() ? "configured" : "disabled",
		settings::GetKickCommand() && *settings::GetKickCommand() ? "configured" : "disabled");
	Msg("[AntiCheat] Discord webhook: %s.\n",
		webhook && webhook->IsDiscordConfigured() ? (webhook->IsDiscordDisabled() ? "disabled after an error" : "configured") : "not configured");
	Msg("[AntiCheat] JSON webhook: %s.\n",
		webhook && webhook->IsJsonConfigured() ? (webhook->IsJsonDisabled() ? "disabled after an error" : "configured") : "not configured");
}

void AntiCheatPlugin::PrintHelp() const
{
	Msg("[AntiCheat] Administrator commands:\n");
	Msg("[AntiCheat] AntiCheat_status - Show the plugin, player, detector, announcement, and punishment status.\n");
	Msg("[AntiCheat] AntiCheat_reload - Reload AntiCheat.cfg and clear transient detector evidence when it finishes.\n");
	Msg("[AntiCheat] AntiCheat_check_config - Check the current settings without changing them.\n");
	Msg("[AntiCheat] AntiCheat_test_announcement - Show a harmless chat and center-screen test without punishing anyone.\n");
	Msg("[AntiCheat] AntiCheat_webhook_test - Send a harmless test report to each configured webhook.\n");
}

void AntiCheatPlugin::ReloadConfig()
{
	if (configReloadPending)
	{
		Msg("[AntiCheat] A configuration load is already in progress. Please wait for it to finish.\n");
		return;
	}
	if (!interfaces::pEngine)
	{
		Msg("[AntiCheat] The configuration could not be reloaded because the server command service is unavailable.\n");
		return;
	}

	configReloadPending = true;
	configLoadFailed = false;
	Msg("[AntiCheat] Reloading AntiCheat.cfg. AntiCheat will confirm when the file reaches its final marker.\n");
	settings::ExecuteConfig();
	StartTimer(ConfigLoadTimeout, 5.0, true, true);
}

void AntiCheatPlugin::OnConfigLoaded()
{
	const bool reloaded = configLoaded;
	configLoaded = true;
	configReloadPending = false;
	configLoadFailed = false;
	lastConfigLoad = std::chrono::steady_clock::now();
	settings::MarkConfigReloaded();
	localization::Reload(settings::GetLanguage());
	if (webhook)
	{
		webhook->Reload();
	}
	PrintConfigSummary(reloaded);
	CheckConfig();
}

void AntiCheatPlugin::CheckConfig() const
{
	Msg("[AntiCheat] Checking the current configuration. Nothing will be changed.\n");
	int findings = 0;
	if (configReloadPending)
	{
		Msg("[AntiCheat] Review AntiCheat.cfg: it is still loading, so this check may not reflect the whole file yet.\n");
		++findings;
	}
	else if (configLoadFailed)
	{
		Msg("[AntiCheat] Review AntiCheat.cfg: the last load did not reach its final AntiCheat_config_loaded marker.\n");
		++findings;
	}
	if (!settings::IsPluginEnabled())
	{
		Msg("[AntiCheat] Review AntiCheat_enabled: detection is currently disabled.\n");
		++findings;
	}
	if (settings::GetRejectedWhitelistCount())
	{
		Msg("[AntiCheat] Review AntiCheat_whitelist: %zu entries were rejected because they are not valid SteamID64 values.\n",
			settings::GetRejectedWhitelistCount());
		++findings;
	}
	if (settings::GetDuplicateWhitelistCount())
	{
		Msg("[AntiCheat] Review AntiCheat_whitelist: %zu duplicate entries were ignored.\n", settings::GetDuplicateWhitelistCount());
		++findings;
	}
	findings += CheckCommandTemplate("AntiCheat_punishment_command", settings::GetPunishmentCommand(), true);
	findings += CheckCommandTemplate("AntiCheat_kick_command", settings::GetKickCommand(), false);
	if (!WebhookService::IsValidUrl(settings::GetWebhookUrl()))
	{
		Msg("[AntiCheat] Review AntiCheat_webhook_url: it is not a Discord webhook URL.\n");
		++findings;
	}
	if (!WebhookService::IsValidJsonUrl(settings::GetJsonWebhookUrl()))
	{
		Msg("[AntiCheat] Review AntiCheat_json_webhook_url: it must be an HTTPS URL.\n");
		++findings;
	}
	if (!WebhookService::IsValidBearerToken(settings::GetJsonWebhookBearerToken()))
	{
		Msg("[AntiCheat] Review AntiCheat_json_webhook_bearer_token: it must not contain whitespace and must be 4096 characters or shorter.\n");
		++findings;
	}
	if (!WebhookService::IsValidRoleId(settings::GetWebhookRoleId()))
	{
		Msg("[AntiCheat] Review AntiCheat_webhook_role_id: it must contain only numbers.\n");
		++findings;
	}
	if (!WebhookService::IsValidLogoUrl(settings::GetWebhookLogoUrl()))
	{
		Msg("[AntiCheat] Review AntiCheat_webhook_logo_url: it must be an HTTPS URL.\n");
		++findings;
	}
	if (!settings::ShowChatAnnouncements() && !settings::ShowCenterAnnouncements())
	{
		Msg("[AntiCheat] Review announcements: chat and center screen are both disabled. Detections will only appear in the server console.\n");
		++findings;
	}
	if (MovementDetectionService::IsSvCheatsTestingAllowed())
	{
		Msg("[AntiCheat] Review sv_cheats testing: inherited movement detections are allowed while sv_cheats is on.\n");
		++findings;
	}

	if (findings)
	{
		Msg("[AntiCheat] Configuration check found %d %s to review.\n", findings, findings == 1 ? "thing" : "things");
	}
	else
	{
		Msg("[AntiCheat] Configuration check passed. Everything is ready.\n");
	}
}

void AntiCheatPlugin::TestAnnouncement() const
{
	Msg("[AntiCheat] Running a harmless announcement test. No detection or punishment will be created.\n");
	utils::AnnounceTest();
}

void AntiCheatPlugin::TestWebhook()
{
	if (webhook)
	{
		webhook->Test();
	}
}

void AntiCheatPlugin::ReportConfigLoadTimeout()
{
	if (!configReloadPending)
	{
		return;
	}
	configReloadPending = false;
	configLoadFailed = true;
	Msg("[AntiCheat] AntiCheat.cfg did not finish loading. Make sure it exists and ends with AntiCheat_config_loaded.\n");
	Msg("[AntiCheat] Some settings may have changed before loading stopped. Run AntiCheat_check_config before continuing.\n");
}

void AntiCheatPlugin::PrintStatus() const
{
	int connected = 0;
	int humans = 0;
	int bots = 0;
	if (g_pAntiCheatPlayerManager)
	{
		for (int index = 1; index <= MAXPLAYERS; ++index)
		{
			auto *player = g_pAntiCheatPlayerManager->ToPlayer(static_cast<u32>(index));
			if (!player || !player->IsConnected() || player->IsCSTV())
			{
				continue;
			}
			++connected;
			player->IsFakeClient() ? ++bots : ++humans;
		}
	}

	std::string enabledNames;
	for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(DetectionType::Count); ++index)
	{
		if (!settings::IsDetectionEnabled(static_cast<DetectionType>(index)))
		{
			continue;
		}
		if (!enabledNames.empty())
		{
			enabledNames += ", ";
		}
		enabledNames += detectionNames[index];
	}

	Msg("[AntiCheat] Status: %s, version %s.\n", loaded ? "running" : "not running", PLUGIN_FULL_VERSION);
	if (!loaded)
	{
		Msg("[AntiCheat] Activation error: %s\n",
			activationError.empty() ? "the game is still starting or no detailed error was recorded" : activationError.c_str());
		return;
	}
	if (configReloadPending)
	{
		Msg("[AntiCheat] Configuration: waiting for AntiCheat.cfg to finish.\n");
	}
	else if (configLoadFailed)
	{
		Msg("[AntiCheat] Configuration: the last load did not finish. Run AntiCheat_check_config after fixing the file.\n");
	}
	else if (!configLoaded)
	{
		Msg("[AntiCheat] Configuration: not confirmed yet.\n");
	}
	else
	{
		const auto age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - lastConfigLoad).count();
		Msg("[AntiCheat] Configuration: loaded %lld seconds ago.\n", static_cast<long long>(age));
	}
	Msg("[AntiCheat] Players: %d connected (%d human, %d bots).\n", connected, humans, bots);
	Msg("[AntiCheat] Detectors: %zu/%zu enabled.\n", settings::GetEnabledDetectionCount(), static_cast<size_t>(DetectionType::Count));
	Msg("[AntiCheat] Enabled detectors: %s.\n", enabledNames.empty() ? "none" : enabledNames.c_str());
	Msg("[AntiCheat] Whitelist: %zu valid entries, %zu rejected, %zu duplicates ignored during the last update.\n", settings::GetWhitelistCount(),
		settings::GetRejectedWhitelistCount(), settings::GetDuplicateWhitelistCount());
	Msg("[AntiCheat] Announcements: chat %s, center screen %s.\n", settings::ShowChatAnnouncements() ? "on" : "off",
		settings::ShowCenterAnnouncements() ? "on" : "off");
	Msg("[AntiCheat] Punishments: permanent ban %s, kick %s.\n",
		settings::GetPunishmentCommand() && *settings::GetPunishmentCommand() ? "configured" : "disabled",
		settings::GetKickCommand() && *settings::GetKickCommand() ? "configured" : "disabled");
	const size_t webhookQueueSize = webhook ? webhook->QueueSize() : 0;
	Msg("[AntiCheat] Discord webhook: %s, %zu queued report%s.\n",
		webhook && webhook->IsDiscordConfigured() ? (webhook->IsDiscordDisabled() ? "disabled after an error" : "configured") : "not configured",
		webhookQueueSize, webhookQueueSize == 1 ? "" : "s");
	Msg("[AntiCheat] JSON webhook: %s.\n",
		webhook && webhook->IsJsonConfigured() ? (webhook->IsJsonDisabled() ? "disabled after an error" : "configured") : "not configured");
	Msg("[AntiCheat] sv_cheats testing: %s.\n", MovementDetectionService::IsSvCheatsTestingAllowed() ? "allowed" : "not allowed");
}

void AntiCheatPlugin::ResetRuntime()
{
	if (configReloadPending)
	{
		configReloadPending = false;
		configLoadFailed = true;
		Msg("[AntiCheat] The configuration load was interrupted by a map reset. Run AntiCheat_reload after the new map is ready.\n");
	}
	simulatingPhysics = false;
	serverGlobals = {};
	hooks::ResetMap();
	utils::ResetDetectionAnnouncement();
	RemoveAllTimers();
	g_ClientCvarValue.OnMapReset();
	detectionSystem.Reset();
	g_pAntiCheatPlayerManager->ResetPlayers();
	punishmentLevels.fill(PunishmentLevel::None);
}

void AntiCheatPlugin::CleanupRuntime(bool finalCleanup)
{
	// Any KHook callback that survives manual cleanup must remain inert
	// until Metamod removes all hooks owned by this plugin.
	loaded = false;
	if (webhook)
	{
		webhook->Unload();
		delete webhook;
		webhook = nullptr;
	}
	bool sourceHooksRemoved = hooks::Cleanup();
	utils::ResetDetectionAnnouncement();
	RemoveAllTimers();
	detectionSystem.Unload();
	g_pClientCvarValue = nullptr;
	sourceHooksRemoved = g_ClientCvarValue.Unload() && sourceHooksRemoved;
	if (!sourceHooksRemoved)
	{
		Warning("[AntiCheat] Some Metamod hooks are still attached but inert. Metamod will remove them before closing AntiCheat.\n");
	}
	if (svCheatsWatcherInstalled)
	{
		MovementDetectionService::CleanupSvCheatsWatcher();
		svCheatsWatcherInstalled = false;
	}
	localization::Shutdown();
	if (finalCleanup)
	{
		settings::Shutdown();
		if (convarsRegistered)
		{
			ConVar_Unregister();
			convarsRegistered = false;
		}
	}
	if (g_pAntiCheatPlayerManager)
	{
		g_pAntiCheatPlayerManager->ResetPlayers();
	}
	simulatingPhysics = false;
	serverGlobals = {};
	punishmentLevels.fill(PunishmentLevel::None);
	joinWatermarks.fill({});
	utils::Cleanup();
	modules::Cleanup();
	activationPending = false;
	configLoaded = false;
	configReloadPending = false;
	configLoadFailed = false;
	lastConfigLoad = {};
	if (finalCleanup)
	{
		activationError.clear();
	}
}
