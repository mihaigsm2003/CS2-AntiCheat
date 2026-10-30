#include "player_context.h"

#include "AntiCheat.h"
#include "movement_analysis/detection/movement_detection.h"
#include "movement_analysis/jump_analysis/jump_analysis.h"
#include "movement_analysis/settings/movement_settings.h"
#include "movement_analysis/events/movement_events.h"
#include "settings.h"

AntiCheatPlayer::~AntiCheatPlayer()
{
	delete movementDetection;
	delete jumpAnalysis;
	delete movementEvents;
}

void AntiCheatPlayer::Init()
{
	delete movementDetection;
	delete jumpAnalysis;
	delete movementEvents;
	movementDetection = new MovementDetectionService(this);
	jumpAnalysis = new JumpAnalysisService(this);
	movementEvents = new MovementEventService(this);
	Reset();
}

void AntiCheatPlayer::Reset()
{
	MovementPlayer::Reset();
	lastTeleportTime = 0.0;
	jumpAnalysisActive = false;
	if (movementDetection)
	{
		movementDetection->Reset();
	}
	if (jumpAnalysis)
	{
		jumpAnalysis->Reset();
	}
	if (movementEvents)
	{
		movementEvents->Reset();
	}
}

void AntiCheatPlayer::OnPlayerFullyConnect()
{
	movementDetection->OnPlayerFullyConnect();
}

void AntiCheatPlayer::OnPhysicsSimulate()
{
	MovementPlayer::OnPhysicsSimulate();
}

void AntiCheatPlayer::OnPhysicsSimulatePost()
{
	MovementPlayer::OnPhysicsSimulatePost();
	movementDetection->OnPhysicsSimulatePost();
}

void AntiCheatPlayer::OnProcessUsercmds(PlayerCommand *commands, int numCommands)
{
	g_AntiCheat.OnProcessUsercmds(this, commands, numCommands);
}

void AntiCheatPlayer::OnSetupMove(PlayerCommand *command)
{
	g_AntiCheat.OnSetupMove(this, command);
	movementDetection->OnSetupMove(command);
}

void AntiCheatPlayer::OnProcessMovement()
{
	MovementPlayer::OnProcessMovement();
	movementDetection->OnProcessMovement();
	const bool shouldAnalyzeJumps =
		settings::IsDetectionEnabled(DetectionType::Autostrafe) && movementDetection->ShouldRunDetections() && !IsFakeClient() && !IsCSTV();
	if (jumpAnalysisActive && !shouldAnalyzeJumps)
	{
		jumpAnalysis->Reset();
	}
	jumpAnalysisActive = shouldAnalyzeJumps;
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnProcessMovement();
	}
}

void AntiCheatPlayer::OnProcessMovementPost()
{
	movementDetection->OnProcessMovementPost();
	if (jumpAnalysisActive)
	{
		jumpAnalysis->UpdateJump();
		jumpAnalysis->OnProcessMovementPost();
	}
	MovementPlayer::OnProcessMovementPost();
}

void AntiCheatPlayer::OnDuck() {}

void AntiCheatPlayer::OnJumpLegacy()
{
	movementEvents->OnJumpLegacy();
}

void AntiCheatPlayer::OnJumpLegacyPost()
{
	movementEvents->OnJumpLegacyPost();
}

void AntiCheatPlayer::OnJumpModern()
{
	movementEvents->OnJumpModern();
}

void AntiCheatPlayer::OnJumpModernPost()
{
	movementEvents->OnJumpModernPost();
}

void AntiCheatPlayer::OnAirMove()
{
	movementDetection->OnAirMove();
}

void AntiCheatPlayer::OnAirAccelerate(Vector &, f32 &, f32 &)
{
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnAirAccelerate();
	}
}

void AntiCheatPlayer::OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel)
{
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnAirAcceleratePost(wishdir, wishspeed, accel);
	}
}

void AntiCheatPlayer::OnTryPlayerMove(Vector *, trace_t *, bool *)
{
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnTryPlayerMove();
	}
}

void AntiCheatPlayer::OnTryPlayerMovePost(Vector *, trace_t *, bool *)
{
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnTryPlayerMovePost();
	}
}

void AntiCheatPlayer::OnStartTouchGround()
{
	movementDetection->CreateLandEvent();
	if (jumpAnalysisActive)
	{
		jumpAnalysis->EndJump();
	}
}

void AntiCheatPlayer::OnStopTouchGround()
{
	if (!inPerf && !GetMovementSetting(MOVEMENT_SETTING_SV_LEGACY_JUMP)->m_bValue)
	{
		f32 landingTick = GetMoveServices()->m_ModernJump().m_nLastLandedTick() + GetMoveServices()->m_ModernJump().m_flLastLandedFrac();
		f32 window = GetMovementSetting(MOVEMENT_SETTING_SV_BHOP_TIME_WINDOW)->m_fl32Value * 0.5f * ENGINE_FIXED_TICK_RATE;
		f32 startTime = currentMoveData->m_flSubtickStartFraction + currentMoveData->m_nTickCount;
		inPerf = startTime >= landingTick - window && startTime <= landingTick + window && jumped;
	}
	if (jumpAnalysisActive)
	{
		jumpAnalysis->AddJump();
	}
}

void AntiCheatPlayer::OnChangeMoveType(MoveType_t oldMoveType)
{
	movementDetection->OnChangeMoveType(oldMoveType);
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnChangeMoveType(oldMoveType);
	}
}

void AntiCheatPlayer::OnTeleport(const Vector *, const QAngle *, const Vector *)
{
	lastTeleportTime = g_pAntiCheatUtils->GetServerGlobals()->curtime;
	if (jumpAnalysisActive)
	{
		jumpAnalysis->HandleTeleport();
	}
}

bool AntiCheatPlayer::JustTeleported(f32 threshold)
{
	return g_pAntiCheatUtils->GetServerGlobals()->curtime - lastTeleportTime < threshold;
}

const CVValue_t *AntiCheatPlayer::GetMovementSetting(MovementSetting setting)
{
	return movement_settings::settingRefs[setting]->GetConVarData()->ValueOrDefault(-1);
}

void AntiCheatPlayer::PrintDebug(const char *format, ...)
{
	char message[1024] {};
	va_list arguments;
	va_start(arguments, format);
	V_vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	Msg("[AntiCheat Nulls] %s\n", message);
}
