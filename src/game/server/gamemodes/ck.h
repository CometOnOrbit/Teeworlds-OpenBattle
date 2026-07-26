/* CK: two-leg attack/defence controller. */
#ifndef GAME_SERVER_GAMEMODES_CK_H
#define GAME_SERVER_GAMEMODES_CK_H

#include <game/server/gamecontroller.h>

class CGameControllerCK : public IGameController
{
	enum { MAX_POINTS = 16, MAX_DOORS = 256, MAX_DOOR_TILES = 256, MAX_TELEPORT_IN_TILES = 64, ROUND_RESULT_DELAY = 8 };
	struct CRoundResult
	{
		bool m_Completed;
		int m_TimeTicks;
		int m_Points;
		int m_Progress;
	};

	class CFlag *m_apFlags[2]; // physical red/blue flags
	bool m_aaPresence[MAX_POINTS][MAX_CLIENTS];
	vec2 m_aPointPos[MAX_POINTS];
	bool m_aPointPresent[MAX_POINTS];
	vec2 m_aaaTeleportIn[2][MAX_POINTS][MAX_TELEPORT_IN_TILES];
	int m_aaNumTeleportInTiles[2][MAX_POINTS];
	vec2 m_aTeleportOut[MAX_POINTS];
	bool m_aaTeleportInPresent[2][MAX_POINTS];
	bool m_aTeleportOutPresent[MAX_POINTS];
	bool m_aPointFlagPresent[MAX_POINTS];
	int m_aTeleportCooldown[MAX_CLIENTS];
	class CCKDoor *m_apDoors[MAX_DOORS];
	int m_NumDoors;
	vec2 m_aDoorTilePos[MAX_DOOR_TILES];
	int m_aDoorTileNumber[MAX_DOOR_TILES];
	int m_NumDoorTiles;
	int m_NumIgnoredLegacyNavigationEntities;
	bool m_DoorsBuilt;
	bool m_TeleportMarkersBuilt;
	float m_aPointProgress[MAX_POINTS];
	int m_aEmptyTicks[MAX_POINTS];
	struct CPointNode
	{
		bool m_Configured;
		unsigned short m_Predecessors;
		unsigned short m_Successors;
		int m_Depth;
		char m_aLabel[32];
	};
	CPointNode m_aNodes[MAX_POINTS];
	unsigned short m_NodeMask;
	unsigned short m_GoalMask;
	bool m_GraphLoaded;
	int m_AttackingTeam;
	int m_BaseHealth;
	bool m_aBaseWarning[3];
	bool m_FinalStage;
	bool m_MapValid;
	bool m_RoundFinished;
	int m_IntermissionTick;
	bool m_ResetMatchOnNextRound;
	CRoundResult m_aResults[2];

	int Defender() const { return m_AttackingTeam ^ 1; }
	int AttackingPhysicalTeam() const { return TEAM_RED; }
	int DefendingPhysicalTeam() const { return TEAM_BLUE; }
	void ScanMap();
	void ResetCKRound();
	void TickPoints();
	void SyncLegacyCheckpointProgress();
	void TickTeleports();
	void TickFlags();
	void FinishRound(const char *pReason, bool Completed);
	void Announce(const char *pText);
	void AnnounceMapResult();
	bool IsAt(vec2 A, vec2 B, float Radius) const;
	void SnapFlags(int SnappingClient);
	void RegisterDoorTile(vec2 Pos, int Number);
	void BuildDoors();
	bool LoadGraphConfig(char *pError, int ErrorSize);
	bool ValidateGraph(char *pError, int ErrorSize);
	bool IsConfigured(int Point) const;
	bool OwnedByAttackers(int Point) const;
	bool CanAttackPoint(int Point) const;
	bool CanRetakePoint(int Point) const;
	void SetAttackerClosure(int Point);
	void SetDefenderClosure(int Point);
	bool AllGoalsCaptured() const;
	int CapturedPointCount() const;
	int AdvanceMetric() const;
	void PointName(int Point, char *pBuf, int BufSize) const;

public:
	CGameControllerCK(class CGameContext *pGameServer);
	virtual void StartRound();
	virtual bool OnEntity(int Index, vec2 Pos);
	virtual bool OnSwitchEntity(int Index, vec2 Pos, int Number, int Flags);
	virtual bool CanSpawn(int Team, vec2 *pPos);
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual bool CanBeMovedOnBalance(int ClientID);
	virtual void RegisterCheckpointPresence(int Checkpoint, int ClientID);
	virtual void OnBaseDamage(vec2 Pos, int Owner, int Damage);
	virtual bool IsDoorClosed(int Number) const;
	virtual int PointFlagTeam(int Number) const;
	virtual bool TeleportEnabled(int Number, int Side) const;
	virtual bool DoorBlocksCharacter(int Number, int Team) const;
	virtual bool CheckpointTileBlocksCharacter(int Number, int Team) const;
	virtual bool IntersectDoor(vec2 From, vec2 To, vec2 *pHit, float Radius) const;
	virtual void SendObjectiveStatus(int ClientID);
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	virtual void DoWincheck();
};

#endif
