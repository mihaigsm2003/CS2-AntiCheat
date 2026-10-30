#pragma once

#include "common.h"
#include "movement/movement.h"

enum MovementSetting : int;

#define AntiCheat_RECENT_TELEPORT_THRESHOLD 0.05f

class AntiCheatPlayer;
class MovementDetectionService;
class JumpAnalysisService;
class MovementEventService;

class PlayerService
{
public:
	explicit PlayerService(AntiCheatPlayer *player) : player(player) {}

	virtual ~PlayerService() = default;

	virtual void Reset() {}

	AntiCheatPlayer *player;
};

class AntiCheatPlayer : public MovementPlayer
{
public:
	explicit AntiCheatPlayer(i32 index) : MovementPlayer(index)
	{
		Init();
	}

	~AntiCheatPlayer() override;

	void Init() override;
	void Reset() override;
	void OnPlayerFullyConnect() override;
	void OnPhysicsSimulate() override;
	void OnPhysicsSimulatePost() override;
	void OnProcessUsercmds(PlayerCommand *commands, int numCommands) override;
	void OnSetupMove(PlayerCommand *command) override;
	void OnProcessMovement() override;
	void OnProcessMovementPost() override;
	void OnDuck() override;
	void OnJumpLegacy() override;
	void OnJumpLegacyPost() override;
	void OnJumpModern() override;
	void OnJumpModernPost() override;
	void OnAirMove() override;
	void OnAirAccelerate(Vector &wishdir, f32 &wishspeed, f32 &accel) override;
	void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel) override;
	void OnTryPlayerMove(Vector *destination, trace_t *trace, bool *surfing) override;
	void OnTryPlayerMovePost(Vector *destination, trace_t *trace, bool *surfing) override;
	void OnStartTouchGround() override;
	void OnStopTouchGround() override;
	void OnChangeMoveType(MoveType_t oldMoveType) override;
	void OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity) override;

	bool JustTeleported(f32 threshold = AntiCheat_RECENT_TELEPORT_THRESHOLD);
	const CVValue_t *GetMovementSetting(MovementSetting setting);
	void PrintDebug(const char *format, ...);

	MovementDetectionService *movementDetection {};
	JumpAnalysisService *jumpAnalysis {};
	MovementEventService *movementEvents {};

private:
	f64 lastTeleportTime {};
	bool jumpAnalysisActive {};
};

class AntiCheatPlayerManager : public MovementPlayerManager
{
public:
	AntiCheatPlayerManager();
	AntiCheatPlayer *ToPlayer(CPlayerPawnComponent *component);
	AntiCheatPlayer *ToPlayer(CBasePlayerController *controller);
	AntiCheatPlayer *ToPlayer(CBasePlayerPawn *pawn);
	AntiCheatPlayer *ToPlayer(CPlayerSlot slot);
	AntiCheatPlayer *ToPlayer(CEntityIndex index);
	AntiCheatPlayer *ToPlayer(CPlayerUserId userID);
	AntiCheatPlayer *ToPlayer(u32 index);
	AntiCheatPlayer *SteamIdToPlayer(u64 steamID, bool validated = true);
};

extern AntiCheatPlayerManager *g_pAntiCheatPlayerManager;
