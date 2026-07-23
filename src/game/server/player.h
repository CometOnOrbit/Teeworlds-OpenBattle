/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

// this include should perhaps be removed
#include "entities/character.h"
#include "gamecontext.h"

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	enum
	{
		BATTLEFIELD_CLASS_NONE = 0,
		BATTLEFIELD_CLASS_SOLDIER,
		BATTLEFIELD_CLASS_ENGINEER,
		BATTLEFIELD_CLASS_MEDIC,
		BATTLEFIELD_CLASS_SNIPER,
	};

	enum
	{
		BATTLEFIELD_WATER_WEAPON_NONE = 0,
		BATTLEFIELD_WATER_WEAPON_HARPOON,
		BATTLEFIELD_WATER_WEAPON_ARROWS,
		BATTLEFIELD_WATER_WEAPON_THROWING_STAR,
	};

	// Session-only UX tip flags (once per connect).
	enum
	{
		TIP_WELCOME = 1<<0,
		TIP_CLASS_REMINDER = 1<<1,
		TIP_CLASS_HINT = 1<<2,
		TIP_VEHICLE = 1<<3,
		TIP_ANTITANK = 1<<4,
		TIP_ANTIAIR = 1<<5,
		TIP_CHECKPOINT = 1<<6,
		TIP_MINE = 1<<7,
		TIP_C4 = 1<<8,
		TIP_AMMO_PACK = 1<<9,
	};

	CPlayer(CGameContext *pGameServer, int ClientID, int Team);
	~CPlayer();

	void Init(int CID);

	void TryRespawn();
	void Respawn();
	void StartRespawn() { m_Spawning = true; }
	void SetTeam(int Team);
	int GetTeam() const { return m_Team; };
	int GetCID() const { return m_ClientID; };

	void Tick();
	void PostTick();
	void Snap(int SnappingClient);

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect(const char *pReason);

	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();

	int GetBattlefieldClass() const;
	void SetBattlefieldClass(int Class);
	bool HasBattlefieldClass() const { return GetBattlefieldClass() != BATTLEFIELD_CLASS_NONE; }
	bool IsSoldier() const { return m_Soldier != 0; }
	bool IsEngineer() const { return m_Engineer != 0; }
	bool IsMedic() const { return m_Medic != 0; }
	bool IsSniper() const { return m_Sniper != 0; }

	int GetWaterWeapon() const;
	void SetWaterWeapon(int Weapon);
	bool HasHarpoon() const { return m_Harpoon != 0; }
	bool HasArrows() const { return m_Arrows != 0; }
	bool HasThrowingStar() const { return m_ThrowingStar != 0; }
	void BeginBattlefieldAimCamera();
	void EndBattlefieldAimCamera();
	bool BattlefieldAimCameraActive() const { return m_BattlefieldAimCamera; }

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int m_SpectatorID;

	bool m_IsReady;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;

	// TODO: clean this up
	struct
	{
		char m_SkinName[64];
		char m_aaSkinPartNames[6][24];
		int m_aUseCustomColors[6];
		int m_aSkinPartColors[6];
		int m_UseCustomColor;
		int m_ColorBody;
		int m_ColorFeet;
	} m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	int m_TeamChangeTick;
	char m_aLanguage[16];
	int m_TipFlags;
	int m_EnterTick;
	bool TryConsumeTip(int Flag);
	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

private:
	// Four independent class flags; separate ints preserve ResetClasses behavior.
	int m_Soldier;
	int m_Medic;
	int m_Engineer;
	int m_Sniper;

	// Persistent water-weapon selection. Ammo belongs to the spawned character.
	int m_Harpoon;
	int m_Arrows;
	int m_ThrowingStar;
	// While set, only the local PlayerInfo is exposed as spectator and
	// direct-input targets drive m_ViewPos in world space.
	bool m_BattlefieldAimCamera;
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	//
	bool m_Spawning;
	int m_ClientID;
	int m_Team;
};

#endif
