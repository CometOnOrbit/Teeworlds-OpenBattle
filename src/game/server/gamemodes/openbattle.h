/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_OPENBATTLE_H
#define GAME_SERVER_GAMEMODES_OPENBATTLE_H
#include <game/server/gamecontroller.h>
#include <game/server/entity.h>

class CGameControllerOpenBattle : public IGameController
{
private:
	enum
	{
		OBJECTIVE_NONE = -1,
		OBJECTIVE_FLAG_OFFENSIVE,
		OBJECTIVE_CENTRAL_DOMINATION,
		OBJECTIVE_BEHIND_LINES,
		OBJECTIVE_BREAKTHROUGH,
		OBJECTIVE_AREA_SUPERIORITY,
		OBJECTIVE_COMBINED_ARMS,
		NUM_OBJECTIVES,
	};

	bool m_aaCheckpointPresence[3][MAX_CLIENTS];
	bool m_aCheckpointAvailable[3];
	bool m_aCheckpointContested[3];
	float m_aCheckpointProgress[3];
	float m_aCheckpointRollbackAnchor[3];
	int m_aCheckpointEmptyTicks[3];
	int m_LastRoundStartTick;
	int m_RoundActiveTicks;
	bool m_MapScanned;
	bool m_CUnlockWarningSent;
	bool m_CUnlockedSent;

	int m_CurrentObjective;
	int m_LastObjective;
	int m_ObjectiveElapsedTicks;
	int m_NextObjectiveStartTick;
	bool m_ObjectivePreannounced;
	bool m_ObjectiveThirtySent;
	int m_aObjectivePool[NUM_OBJECTIVES];
	int m_ObjectivePoolSize;
	int m_ObjectivePoolPos;
	bool m_aObjectiveFirstReward[2];
	int m_aaObjectiveControlTicks[2][3];
	int m_aObjectiveHoldTicks[2];
	int m_aObjectiveCooldownTicks[2];
	int m_aObjectiveRewardCount[2];
	int m_aObjectiveAreaMask[2];
	bool m_aObjectiveCompleted[2];
	int m_aObjectiveStage[2];
	int m_ComebackTeam;
	bool m_ComebackActive;

	void ResetRoundState();
	void ScanMapObjectives();
	void TickCheckpoints();
	void CompleteCheckpointCapture(int Checkpoint, int Team, int ClientID);
	int CheckpointOwner(int Checkpoint) const;
	bool CheckpointControlled(int Checkpoint, int Team) const;
	void TickDynamicObjectives();
	void ResetObjectiveState();
	void RefillObjectivePool();
	int PickNextObjective();
	bool ObjectiveEligible(int Objective) const;
	int ObjectiveDurationSeconds(int Objective) const;
	const char *ObjectiveName(int Objective) const;
	void StartObjective(int Objective);
	void EndObjective(bool SuddenDeath = false);
	void TickObjectiveRules();
	void AddDynamicTeamScore(int Team, int Amount, const char *pReason);
	void AnnounceObjective(const char *pFormat, int Objective, int Sound);
	void AnnounceTeamCompletion(int Team);
	void DetermineComebackTeam();
	void OnFlagCaptured(int Team, int ClientID);

public:
	class CFlag *m_apFlags[2];

	CGameControllerOpenBattle(class CGameContext *pGameServer);
	virtual void DoWincheck();
	virtual bool CanBeMovedOnBalance(int ClientID);
	virtual void Snap(int SnappingClient);
	virtual void Tick();

	virtual bool OnEntity(int Index, vec2 Pos);
	virtual void RegisterCheckpointPresence(int Checkpoint, int ClientID);
	virtual void SendObjectiveStatus(int ClientID);
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
};

#endif
