#include "settings.h"

#include "convar.h"
#include "eiface.h"
#include "utils/interfaces.h"

#include <algorithm>
#include <charconv>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	std::size_t rejectedWhitelistEntries {};
	std::size_t duplicateWhitelistEntries {};
	std::uint64_t settingsRevision {1};
	std::uint64_t detectionMask {};
	bool detectionMaskDirty {true};
	bool pluginEnabled {};

	std::vector<std::uint64_t> &WhitelistedSteamIds()
	{
		static std::vector<std::uint64_t> steamIds;
		return steamIds;
	}

	void BumpRevision()
	{
		if (++settingsRevision == 0)
		{
			settingsRevision = 1;
		}
	}

	void OnDetectionSettingChanged(CConVar<bool> *, CSplitScreenSlot, const bool *, const bool *)
	{
		detectionMaskDirty = true;
		BumpRevision();
	}

	void OnWhitelistChanged(CConVar<CUtlString> *, CSplitScreenSlot, const CUtlString *newValue, const CUtlString *)
	{
		auto &steamIds = WhitelistedSteamIds();
		steamIds.clear();
		rejectedWhitelistEntries = 0;
		duplicateWhitelistEntries = 0;
		std::string value = newValue ? newValue->Get() : "";
		std::replace(value.begin(), value.end(), ',', ' ');
		std::replace(value.begin(), value.end(), ';', ' ');
		std::istringstream entries(value);
		for (std::string entry; entries >> entry;)
		{
			std::uint64_t steamId = 0;
			const auto parsed = std::from_chars(entry.data(), entry.data() + entry.size(), steamId);
			if (parsed.ec == std::errc() && parsed.ptr == entry.data() + entry.size() && steamId != 0)
			{
				steamIds.push_back(steamId);
			}
			else
			{
				++rejectedWhitelistEntries;
			}
		}
		std::sort(steamIds.begin(), steamIds.end());
		const std::size_t parsedEntries = steamIds.size();
		steamIds.erase(std::unique(steamIds.begin(), steamIds.end()), steamIds.end());
		duplicateWhitelistEntries = parsedEntries - steamIds.size();
		BumpRevision();
	}

	struct Configuration
	{
		CConVar<bool> enabled {"AntiCheat_enabled", FCVAR_NONE, "Enable or disable AntiCheat", true, OnDetectionSettingChanged};
		CConVar<bool> aimbotEnabled {"AntiCheat_aimbot_enabled", FCVAR_NONE, "Detect damaging visible aim snaps", true, OnDetectionSettingChanged};
		CConVar<bool> aimlockEnabled {"AntiCheat_aimlock_enabled", FCVAR_NONE, "Detect unnaturally precise target tracking", true,
									  OnDetectionSettingChanged};
		CConVar<bool> antiaimEnabled {"AntiCheat_antiaim_enabled", FCVAR_NONE, "Detect impossible or manipulated view angles", true,
									  OnDetectionSettingChanged};
		CConVar<bool> autostrafeEnabled {"AntiCheat_autostrafe_enabled", FCVAR_NONE, "Detect automated air strafing", true, OnDetectionSettingChanged};
		CConVar<bool> bhopEnabled {"AntiCheat_bhop_enabled", FCVAR_NONE, "Detect automated bunny hopping", true, OnDetectionSettingChanged};
		CConVar<bool> dllInjectionEnabled {"AntiCheat_dll_injection_enabled", FCVAR_NONE, "Detect suspicious client event subscriptions", true,
										   OnDetectionSettingChanged};
		CConVar<bool> desubtickingEnabled {"AntiCheat_desubticking_enabled", FCVAR_NONE, "Detect commands that remove normal subtick timing", true,
										   OnDetectionSettingChanged};
		CConVar<bool> doubletapEnabled {"AntiCheat_doubletap_enabled", FCVAR_NONE, "Detect impossible rapid fire", true, OnDetectionSettingChanged};
		CConVar<bool> hyperscrollEnabled {"AntiCheat_hyperscroll_enabled", FCVAR_NONE, "Detect automated jump-input frequency", true,
										  OnDetectionSettingChanged};
		CConVar<bool> inhumanAccuracyEnabled {"AntiCheat_inhuman_accuracy_enabled", FCVAR_NONE, "Detect sustained near-perfect accuracy", true,
											  OnDetectionSettingChanged};
		CConVar<bool> invalidCvarEnabled {"AntiCheat_invalid_cvar_enabled", FCVAR_NONE, "Detect unsafe client settings", true, OnDetectionSettingChanged};
		CConVar<bool> invalidInputEnabled {"AntiCheat_invalid_input_enabled", FCVAR_NONE,
										   "Detect movement button changes without matching subtick records", true, OnDetectionSettingChanged};
		CConVar<bool> irregularBehaviorEnabled {"AntiCheat_irregular_behavior_enabled", FCVAR_NONE,
												"Detect repeated success with unusually difficult shots", true, OnDetectionSettingChanged};
		CConVar<bool> namechangerEnabled {"AntiCheat_namechanger_enabled", FCVAR_NONE, "Detect repeated player name changes", true,
										  OnDetectionSettingChanged};
		CConVar<bool> nullsEnabled {"AntiCheat_nulls_enabled", FCVAR_NONE, "Detect mechanically perfect airborne opposite-direction switches", true,
									OnDetectionSettingChanged};
		CConVar<bool> silentaimEnabled {"AntiCheat_silentaim_enabled", FCVAR_NONE, "Detect damaging shots that disagree with the visible aim", true,
										OnDetectionSettingChanged};
		CConVar<bool> subtickSpamEnabled {"AntiCheat_subtick_spam_enabled", FCVAR_NONE,
										  "Detect repeated same-time button aliases carrying pitch or yaw changes", true, OnDetectionSettingChanged};
		CConVar<bool> triggerbotEnabled {"AntiCheat_triggerbot_enabled", FCVAR_NONE, "Detect repeated inhuman reactions to fresh crosshair contact", true,
										 OnDetectionSettingChanged};
		CConVar<bool> chatAnnouncements {"AntiCheat_chat_announcements", FCVAR_NONE, "Show AntiCheat detections in public chat", true};
		CConVar<bool> centerAnnouncements {"AntiCheat_center_announcements", FCVAR_NONE, "Show AntiCheat detections in the center of the screen", true};
		CConVar<CUtlString> punishmentCommand {"AntiCheat_punishment_command", FCVAR_NONE, "Command run for permanent-ban detections",
											   CUtlString("css_addban {steamid64} 0 AntiCheat: {detection}")};
		CConVar<CUtlString> kickCommand {"AntiCheat_kick_command", FCVAR_NONE, "Command run for kick-only detections",
										 CUtlString("css_kick #{userid} AntiCheat: {detection}")};
		CConVar<CUtlString> webhookUrl {"AntiCheat_webhook_url", FCVAR_PROTECTED, "Discord webhook URL for detection reports", CUtlString("")};
		CConVar<CUtlString> jsonWebhookUrl {"AntiCheat_json_webhook_url", FCVAR_PROTECTED, "HTTPS endpoint for JSON detection reports", CUtlString("")};
		CConVar<CUtlString> jsonWebhookBearerToken {"AntiCheat_json_webhook_bearer_token", FCVAR_PROTECTED,
			"Optional bearer token for JSON detection reports", CUtlString("")};
		CConVar<CUtlString> webhookRoleId {"AntiCheat_webhook_role_id", FCVAR_NONE, "Discord role ID mentioned in detection reports", CUtlString("")};
		CConVar<CUtlString> webhookServerAddress {"AntiCheat_webhook_server_address", FCVAR_NONE, "Public server address shown in Discord reports",
												  CUtlString("")};
		CConVar<CUtlString> webhookLogoUrl {"AntiCheat_webhook_logo_url", FCVAR_NONE, "Public HTTPS URL for the logo shown in Discord reports",
											CUtlString("")};
		CConVar<CUtlString> language {"AntiCheat_language", FCVAR_NONE, "Language used for public messages and Discord reports", CUtlString("en")};
		CConVar<CUtlString> whitelist {"AntiCheat_whitelist", FCVAR_NONE, "SteamID64s that AntiCheat may detect but never punish", CUtlString(""),
									   OnWhitelistChanged};
	};

	Configuration *configuration {};

	bool DetectionSetting(DetectionType detection)
	{
		if (!configuration)
		{
			return false;
		}
		switch (detection)
		{
			case DetectionType::Aimbot:
				return configuration->aimbotEnabled.GetBool();
			case DetectionType::Aimlock:
				return configuration->aimlockEnabled.GetBool();
			case DetectionType::AntiAim:
				return configuration->antiaimEnabled.GetBool();
			case DetectionType::Autostrafe:
				return configuration->autostrafeEnabled.GetBool();
			case DetectionType::Bhop:
				return configuration->bhopEnabled.GetBool();
			case DetectionType::DllInjection:
				return configuration->dllInjectionEnabled.GetBool();
			case DetectionType::Desubticking:
				return configuration->desubtickingEnabled.GetBool();
			case DetectionType::Doubletap:
				return configuration->doubletapEnabled.GetBool();
			case DetectionType::Hyperscroll:
				return configuration->hyperscrollEnabled.GetBool();
			case DetectionType::InhumanAccuracy:
				return configuration->inhumanAccuracyEnabled.GetBool();
			case DetectionType::InvalidCvar:
				return configuration->invalidCvarEnabled.GetBool();
			case DetectionType::InvalidInput:
				return configuration->invalidInputEnabled.GetBool();
			case DetectionType::IrregularBehavior:
				return configuration->irregularBehaviorEnabled.GetBool();
			case DetectionType::NameChanger:
				return configuration->namechangerEnabled.GetBool();
			case DetectionType::Nulls:
				return configuration->nullsEnabled.GetBool();
			case DetectionType::SilentAim:
				return configuration->silentaimEnabled.GetBool();
			case DetectionType::SubtickSpam:
				return configuration->subtickSpamEnabled.GetBool();
			case DetectionType::Triggerbot:
				return configuration->triggerbotEnabled.GetBool();
			case DetectionType::Count:
				return false;
		}
		return false;
	}
} // namespace

bool settings::Initialize()
{
	if (!configuration)
	{
		configuration = new (std::nothrow) Configuration;
		detectionMaskDirty = true;
	}
	return configuration != nullptr;
}

void settings::Shutdown()
{
	delete configuration;
	configuration = nullptr;
	WhitelistedSteamIds().clear();
	rejectedWhitelistEntries = 0;
	duplicateWhitelistEntries = 0;
	detectionMask = 0;
	detectionMaskDirty = true;
	pluginEnabled = false;
}

bool settings::IsPluginEnabled()
{
	GetDetectionMask();
	return pluginEnabled;
}

bool settings::IsDetectionEnabled(DetectionType detection)
{
	const auto index = static_cast<std::uint8_t>(detection);
	return index < static_cast<std::uint8_t>(DetectionType::Count) && (GetDetectionMask() & (std::uint64_t {1} << index)) != 0;
}

bool settings::IsPlayerWhitelisted(std::uint64_t steamId)
{
	const auto &steamIds = WhitelistedSteamIds();
	return steamId != 0 && std::binary_search(steamIds.begin(), steamIds.end(), steamId);
}

std::size_t settings::GetWhitelistCount()
{
	return WhitelistedSteamIds().size();
}

std::size_t settings::GetRejectedWhitelistCount()
{
	return rejectedWhitelistEntries;
}

std::size_t settings::GetDuplicateWhitelistCount()
{
	return duplicateWhitelistEntries;
}

std::size_t settings::GetEnabledDetectionCount()
{
	const std::uint64_t mask = GetDetectionMask();
	std::size_t count = 0;
	for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(DetectionType::Count); ++index)
	{
		count += (mask >> index) & 1;
	}
	return count;
}

std::uint64_t settings::GetDetectionMask()
{
	if (!detectionMaskDirty)
	{
		return detectionMask;
	}

	detectionMask = 0;
	pluginEnabled = configuration && configuration->enabled.GetBool();
	if (pluginEnabled)
	{
		for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(DetectionType::Count); ++index)
		{
			if (DetectionSetting(static_cast<DetectionType>(index)))
			{
				detectionMask |= std::uint64_t {1} << index;
			}
		}
	}
	detectionMaskDirty = false;
	return detectionMask;
}

std::uint64_t settings::GetRevision()
{
	return settingsRevision;
}

bool settings::ShowChatAnnouncements()
{
	return configuration && configuration->chatAnnouncements.GetBool();
}

bool settings::ShowCenterAnnouncements()
{
	return configuration && configuration->centerAnnouncements.GetBool();
}

const char *settings::GetPunishmentCommand()
{
	return configuration ? configuration->punishmentCommand.Get().Get() : "";
}

const char *settings::GetKickCommand()
{
	return configuration ? configuration->kickCommand.Get().Get() : "";
}

const char *settings::GetWebhookUrl()
{
	return configuration ? configuration->webhookUrl.Get().Get() : "";
}

const char *settings::GetJsonWebhookUrl()
{
	return configuration ? configuration->jsonWebhookUrl.Get().Get() : "";
}

const char *settings::GetJsonWebhookBearerToken()
{
	return configuration ? configuration->jsonWebhookBearerToken.Get().Get() : "";
}

const char *settings::GetWebhookRoleId()
{
	return configuration ? configuration->webhookRoleId.Get().Get() : "";
}

const char *settings::GetWebhookServerAddress()
{
	return configuration ? configuration->webhookServerAddress.Get().Get() : "";
}

const char *settings::GetWebhookLogoUrl()
{
	return configuration ? configuration->webhookLogoUrl.Get().Get() : "";
}

const char *settings::GetLanguage()
{
	return configuration ? configuration->language.Get().Get() : "en";
}

void settings::MarkConfigReloaded()
{
	BumpRevision();
}

void settings::ExecuteConfig()
{
	if (interfaces::pEngine)
	{
		interfaces::pEngine->ServerCommand("exec AntiCheat.cfg\n");
	}
}
