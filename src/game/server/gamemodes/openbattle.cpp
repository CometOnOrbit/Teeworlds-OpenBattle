/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <base/math.h>
#include <base/system.h>

#include <game/mapitems.h>
#include <game/collision.h>
#include <game/generated/protocol7.h>

#include <game/server/entities/character.h>
#include <game/server/entities/flag.h>
#include <game/server/player.h>
#include <game/server/gamecontext.h>
#include "openbattle.h"

CGameControllerOpenBattle::CGameControllerOpenBattle(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
	m_apFlags[0] = 0;
	m_apFlags[1] = 0;
	// Keep the CTF rules/controller but publish this derived mode as OpenBattle.
	// That also exempts its custom tuning from CheckPureTuning().
	m_pGameType = "OpenBattle|CTF";
	m_GameFlags = GAMEFLAG_TEAMS|GAMEFLAG_FLAGS;
	m_LastRoundStartTick = -1;
	m_MapScanned = false;
	m_CurrentObjective = OBJECTIVE_NONE;
	m_LastObjective = OBJECTIVE_NONE;
	m_ObjectivePoolSize = 0;
	m_ObjectivePoolPos = 0;
	mem_zero(m_aaCheckpointPresence, sizeof(m_aaCheckpointPresence));
	mem_zero(m_aCheckpointAvailable, sizeof(m_aCheckpointAvailable));
	ResetRoundState();
}

void CGameControllerOpenBattle::ResetRoundState()
{
	m_RoundActiveTicks = 0;
	m_CUnlockWarningSent = false;
	m_CUnlockedSent = false;
	mem_zero(m_aaCheckpointPresence, sizeof(m_aaCheckpointPresence));
	for(int i = 0; i < 3; i++)
	{
		m_aCheckpointProgress[i] = i == 0 ? -400.0f : (i == 1 ? 400.0f : 0.0f);
		m_aCheckpointRollbackAnchor[i] = m_aCheckpointProgress[i];
		m_aCheckpointEmptyTicks[i] = 0;
		m_aCheckpointContested[i] = false;
		GameServer()->m_aCheckpointState[i] = (int)m_aCheckpointProgress[i];
		GameServer()->m_aCheckpointCaptured[i] = i != 2;
		GameServer()->m_aCheckpointWarning[i][TEAM_RED] = false;
		GameServer()->m_aCheckpointWarning[i][TEAM_BLUE] = false;
	}
	m_CurrentObjective = OBJECTIVE_NONE;
	m_LastObjective = OBJECTIVE_NONE;
	m_ObjectiveElapsedTicks = 0;
	m_NextObjectiveStartTick = 90*Server()->TickSpeed();
	m_ObjectivePreannounced = false;
	m_ObjectiveThirtySent = false;
	m_ObjectivePoolSize = 0;
	m_ObjectivePoolPos = 0;
	m_ComebackTeam = -1;
	m_ComebackActive = false;
	ResetObjectiveState();
}

void CGameControllerOpenBattle::ScanMapObjectives()
{
	CCollision *pCollision = GameServer()->Collision();
	int NumTiles = pCollision->GetWidth()*pCollision->GetHeight();
	for(int i = 0; i < NumTiles; i++)
	{
		int Tile = pCollision->GetTileIndex(i);
		if(Tile >= TILE_BF_CHECKPOINT1 && Tile <= TILE_BF_CHECKPOINT3)
			m_aCheckpointAvailable[Tile-TILE_BF_CHECKPOINT1] = true;
	}
	m_MapScanned = true;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "objective map scan flags='%d%d' checkpoints='%d%d%d'",
		m_apFlags[TEAM_RED] != 0, m_apFlags[TEAM_BLUE] != 0,
		m_aCheckpointAvailable[0], m_aCheckpointAvailable[1], m_aCheckpointAvailable[2]);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "openbattle", aBuf);
}

void CGameControllerOpenBattle::RegisterCheckpointPresence(int Checkpoint, int ClientID)
{
	if(Checkpoint < 0 || Checkpoint >= 3 || ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	CCharacter *pCharacter = pPlayer ? pPlayer->GetCharacter() : 0;
	if(!pPlayer || !pCharacter || !pCharacter->IsAlive() || pPlayer->GetTeam() == TEAM_SPECTATORS || !pPlayer->HasBattlefieldClass())
		return;
	m_aCheckpointAvailable[Checkpoint] = true;
	m_aaCheckpointPresence[Checkpoint][ClientID] = true;
}

int CGameControllerOpenBattle::CheckpointOwner(int Checkpoint) const
{
	if(Checkpoint < 0 || Checkpoint >= 3 || m_aCheckpointContested[Checkpoint])
		return -1;
	if(m_aCheckpointProgress[Checkpoint] <= -399.999f)
		return TEAM_RED;
	if(m_aCheckpointProgress[Checkpoint] >= 399.999f)
		return TEAM_BLUE;
	return -1;
}

bool CGameControllerOpenBattle::CheckpointControlled(int Checkpoint, int Team) const
{
	return CheckpointOwner(Checkpoint) == Team;
}

void CGameControllerOpenBattle::CompleteCheckpointCapture(int Checkpoint, int Team, int ClientID)
{
	GameServer()->m_aCheckpointCaptured[Checkpoint] = true;
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && GameServer()->m_apPlayers[ClientID])
		GameServer()->m_apPlayers[ClientID]->m_Score += 5;
	AddTeamScore(Team, 20);
	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);

	char aBuf[256];
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameServer()->m_apPlayers[i])
			continue;
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("%s-Team (%s) captured Checkpoint %c!", i),
			GameServer()->Localize(Team == TEAM_RED ? "Red" : "Blue", i),
			ClientID >= 0 ? Server()->ClientName(ClientID) : "server", 'A'+Checkpoint);
		GameServer()->SendChatTarget(i, aBuf);
	}
	str_format(aBuf, sizeof(aBuf), "checkpoint_capture point='%c' team='%d' player='%d'", 'A'+Checkpoint, Team, ClientID);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "openbattle", aBuf);

	if(!m_SuddenDeath && !m_ObjectivePreannounced && m_CurrentObjective == OBJECTIVE_CENTRAL_DOMINATION && Checkpoint == 2 && !m_aObjectiveFirstReward[Team])
	{
		m_aObjectiveFirstReward[Team] = true;
		AddDynamicTeamScore(Team, 20, "central_first_capture");
		if(ClientID >= 0 && GameServer()->m_apPlayers[ClientID])
			GameServer()->m_apPlayers[ClientID]->m_Score += 2;
		AnnounceTeamCompletion(Team);
	}
	if(!m_SuddenDeath && !m_ObjectivePreannounced && m_CurrentObjective == OBJECTIVE_BEHIND_LINES && Checkpoint == (Team == TEAM_RED ? 1 : 0) && !m_aObjectiveFirstReward[Team])
	{
		m_aObjectiveFirstReward[Team] = true;
		AddDynamicTeamScore(Team, 30, "behind_lines_first_capture");
		if(ClientID >= 0 && GameServer()->m_apPlayers[ClientID])
			GameServer()->m_apPlayers[ClientID]->m_Score += 2;
		AnnounceTeamCompletion(Team);
	}
}

void CGameControllerOpenBattle::TickCheckpoints()
{
	const int TickSpeed = Server()->TickSpeed();
	for(int Checkpoint = 0; Checkpoint < 3; Checkpoint++)
	{
		int aCount[2] = {0, 0};
		int aFirstClient[2] = {-1, -1};
		for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
		{
			if(!m_aaCheckpointPresence[Checkpoint][ClientID])
				continue;
			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
			CCharacter *pCharacter = pPlayer ? pPlayer->GetCharacter() : 0;
			if(!pPlayer || !pCharacter || !pCharacter->IsAlive() || !pPlayer->HasBattlefieldClass() || pPlayer->GetTeam() == TEAM_SPECTATORS)
				continue;
			int Team = pPlayer->GetTeam();
			aCount[Team]++;
			if(aFirstClient[Team] == -1)
				aFirstClient[Team] = ClientID;
		}

		float OldProgress = m_aCheckpointProgress[Checkpoint];
		int OldOwner = OldProgress <= -399.999f ? TEAM_RED : (OldProgress >= 399.999f ? TEAM_BLUE : -1);
		bool Locked = Checkpoint == 2 && m_RoundActiveTicks < 20*TickSpeed;
		m_aCheckpointContested[Checkpoint] = !Locked && aCount[TEAM_RED] > 0 && aCount[TEAM_BLUE] > 0;
		if(!Locked && !m_aCheckpointContested[Checkpoint] && (aCount[TEAM_RED] > 0 || aCount[TEAM_BLUE] > 0))
		{
			int Team = aCount[TEAM_RED] > 0 ? TEAM_RED : TEAM_BLUE;
			int Count = aCount[Team];
			float Multiplier = Count == 1 ? 1.0f : (Count == 2 ? 1.5f : 2.0f);
			float Seconds = Checkpoint == 2 ? 14.0f : 8.0f;
			float Step = (400.0f/(Seconds*TickSpeed))*Multiplier*(Team == TEAM_RED ? -1.0f : 1.0f);
			m_aCheckpointEmptyTicks[Checkpoint] = 0;
			bool AttackingEnemyControl = (Team == TEAM_RED && OldProgress > 0.0f) || (Team == TEAM_BLUE && OldProgress < 0.0f);
			if(AttackingEnemyControl && !GameServer()->m_aCheckpointWarning[Checkpoint][Team] && GameServer()->m_aCheckpointWarningCooldown[Team] == 0)
			{
				char aBuf[256];
				for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
				{
					CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
					if(!pPlayer || pPlayer->GetTeam() != (Team^1))
						continue;
					str_format(aBuf, sizeof(aBuf), GameServer()->Localize(Team == TEAM_RED ? "[Warning] Red-Team is attacking Checkpoint %c!" : "[Warning] Blue-Team is attacking Checkpoint %c!", ClientID), 'A'+Checkpoint);
					GameServer()->SendChatTarget(ClientID, aBuf);
				}
				GameServer()->m_aCheckpointWarning[Checkpoint][Team] = true;
				GameServer()->m_aCheckpointWarningCooldown[Team] = 15*TickSpeed;
			}
			if(!CheckpointControlled(Checkpoint, Team))
			{
				m_aCheckpointProgress[Checkpoint] = clamp(m_aCheckpointProgress[Checkpoint]+Step, -400.0f, 400.0f);
				if(absolute(m_aCheckpointProgress[Checkpoint]) < 0.001f)
					m_aCheckpointProgress[Checkpoint] = 0.0f;
				else if(m_aCheckpointProgress[Checkpoint] > 399.999f)
					m_aCheckpointProgress[Checkpoint] = 400.0f;
				else if(m_aCheckpointProgress[Checkpoint] < -399.999f)
					m_aCheckpointProgress[Checkpoint] = -400.0f;
				if((OldProgress < 0.0f && m_aCheckpointProgress[Checkpoint] >= 0.0f) ||
					(OldProgress > 0.0f && m_aCheckpointProgress[Checkpoint] <= 0.0f))
				{
					m_aCheckpointRollbackAnchor[Checkpoint] = 0.0f;
					GameServer()->m_aCheckpointCaptured[Checkpoint] = false;
				}
			}
		}
		else if(!Locked && !m_aCheckpointContested[Checkpoint] && aCount[TEAM_RED] == 0 && aCount[TEAM_BLUE] == 0)
		{
			m_aCheckpointEmptyTicks[Checkpoint]++;
			if(m_aCheckpointEmptyTicks[Checkpoint] > 3*TickSpeed)
			{
				float Seconds = Checkpoint == 2 ? 14.0f : 8.0f;
				float Step = 200.0f/(Seconds*TickSpeed);
				float Anchor = m_aCheckpointRollbackAnchor[Checkpoint];
				if(m_aCheckpointProgress[Checkpoint] < Anchor)
					m_aCheckpointProgress[Checkpoint] = min(Anchor, m_aCheckpointProgress[Checkpoint]+Step);
				else if(m_aCheckpointProgress[Checkpoint] > Anchor)
					m_aCheckpointProgress[Checkpoint] = max(Anchor, m_aCheckpointProgress[Checkpoint]-Step);
			}
		}

		if(m_aCheckpointProgress[Checkpoint] == 0.0f)
		{
			GameServer()->m_aCheckpointCaptured[Checkpoint] = false;
			GameServer()->m_aCheckpointWarning[Checkpoint][TEAM_RED] = false;
			GameServer()->m_aCheckpointWarning[Checkpoint][TEAM_BLUE] = false;
		}
		int NewOwner = CheckpointOwner(Checkpoint);
		if(NewOwner != -1)
		{
			float Endpoint = NewOwner == TEAM_RED ? -400.0f : 400.0f;
			bool NewlyCaptured = absolute(m_aCheckpointRollbackAnchor[Checkpoint]-Endpoint) > 0.001f;
			m_aCheckpointRollbackAnchor[Checkpoint] = NewOwner == TEAM_RED ? -400.0f : 400.0f;
			GameServer()->m_aCheckpointCaptured[Checkpoint] = true;
			GameServer()->m_aCheckpointWarning[Checkpoint][TEAM_RED] = false;
			GameServer()->m_aCheckpointWarning[Checkpoint][TEAM_BLUE] = false;
			if(OldOwner != NewOwner && NewlyCaptured)
				CompleteCheckpointCapture(Checkpoint, NewOwner, aFirstClient[NewOwner]);
		}

		int OldMilestone = round_to_int(OldProgress)/80;
		int NewMilestone = round_to_int(m_aCheckpointProgress[Checkpoint])/80;
		if(OldMilestone != NewMilestone && GameServer()->m_aCheckpointProgressSoundCooldown[Checkpoint] == 0)
		{
			GameServer()->CreateSoundGlobal(SOUND_HOOK_NOATTACH);
			GameServer()->m_aCheckpointProgressSoundCooldown[Checkpoint] = (int)(TickSpeed*0.8f+0.5f);
		}
		GameServer()->m_aCheckpointState[Checkpoint] = round_to_int(m_aCheckpointProgress[Checkpoint]);
		for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
			m_aaCheckpointPresence[Checkpoint][ClientID] = false;
	}
}

bool CGameControllerOpenBattle::OnEntity(int Index, vec2 Pos)
{
	if(IGameController::OnEntity(Index, Pos))
		return true;

	int Team = -1;
	if(Index == ENTITY_FLAGSTAND_RED) Team = TEAM_RED;
	if(Index == ENTITY_FLAGSTAND_BLUE) Team = TEAM_BLUE;
	if(Team == -1 || m_apFlags[Team])
		return false;

	CFlag *F = new CFlag(&GameServer()->m_World, Team);
	F->m_StandPos = Pos;
	F->m_Pos = Pos;
	m_apFlags[Team] = F;
	GameServer()->m_World.InsertEntity(F);
	return true;
}

int CGameControllerOpenBattle::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int WeaponID)
{
	IGameController::OnCharacterDeath(pVictim, pKiller, WeaponID);
	int HadFlag = 0;

	// drop flags
	for(int i = 0; i < 2; i++)
	{
		CFlag *F = m_apFlags[i];
		if(F && pKiller && pKiller->GetCharacter() && F->m_pCarryingCharacter == pKiller->GetCharacter())
			HadFlag |= 2;
		if(F && F->m_pCarryingCharacter == pVictim)
		{
			GameServer()->CreateSoundGlobal(SOUND_CTF_DROP);
			F->m_DropTick = Server()->Tick();
			F->m_pCarryingCharacter = 0;
			F->m_Vel = vec2(0,0);

			if(pKiller && pKiller->GetTeam() != pVictim->GetPlayer()->GetTeam())
				pKiller->m_Score++;

			HadFlag |= 1;
		}
	}

	return HadFlag;
}

void CGameControllerOpenBattle::DoWincheck()
{
	if(m_GameOverTick == -1 && !m_Warmup)
	{
		// check score win condition
		if((g_Config.m_SvScorelimit > 0 && (m_aTeamscore[TEAM_RED] >= g_Config.m_SvScorelimit || m_aTeamscore[TEAM_BLUE] >= g_Config.m_SvScorelimit)) ||
			(g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60))
		{
			if(m_SuddenDeath)
			{
				if(m_aTeamscore[TEAM_RED]/100 != m_aTeamscore[TEAM_BLUE]/100)
					EndRound();
			}
			else
			{
				if(m_aTeamscore[TEAM_RED] != m_aTeamscore[TEAM_BLUE])
					EndRound();
				else
					m_SuddenDeath = 1;
			}
		}
	}
}

bool CGameControllerOpenBattle::CanBeMovedOnBalance(int ClientID)
{
	CCharacter* Character = GameServer()->m_apPlayers[ClientID]->GetCharacter();
	if(Character)
	{
		for(int fi = 0; fi < 2; fi++)
		{
			CFlag *F = m_apFlags[fi];
			if(F && F->m_pCarryingCharacter == Character)
				return false;
		}
	}
	return true;
}

void CGameControllerOpenBattle::Snap(int SnappingClient)
{
	IGameController::Snap(SnappingClient);

	if(SnappingClient >= 0 && Server()->IsSixup(SnappingClient))
	{
		protocol7::CNetObj_GameDataTeam *pTeam = (protocol7::CNetObj_GameDataTeam *)Server()->SnapNewItem(
			-protocol7::NETOBJTYPE_GAMEDATATEAM, 0, sizeof(protocol7::CNetObj_GameDataTeam));
		if(pTeam)
		{
			pTeam->m_TeamscoreRed = m_aTeamscore[TEAM_RED];
			pTeam->m_TeamscoreBlue = m_aTeamscore[TEAM_BLUE];
		}

		protocol7::CNetObj_GameDataFlag *pFlag = (protocol7::CNetObj_GameDataFlag *)Server()->SnapNewItem(
			-protocol7::NETOBJTYPE_GAMEDATAFLAG, 0, sizeof(protocol7::CNetObj_GameDataFlag));
		if(!pFlag)
			return;

		pFlag->m_FlagDropTickRed = 0;
		pFlag->m_FlagDropTickBlue = 0;
		if(m_apFlags[TEAM_RED])
		{
			if(m_apFlags[TEAM_RED]->m_AtStand)
				pFlag->m_FlagCarrierRed = protocol7::FLAG_ATSTAND;
			else if(m_apFlags[TEAM_RED]->m_pCarryingCharacter && m_apFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer())
				pFlag->m_FlagCarrierRed = m_apFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer()->GetCID();
			else
				pFlag->m_FlagCarrierRed = protocol7::FLAG_TAKEN;
		}
		else
			pFlag->m_FlagCarrierRed = protocol7::FLAG_MISSING;

		if(m_apFlags[TEAM_BLUE])
		{
			if(m_apFlags[TEAM_BLUE]->m_AtStand)
				pFlag->m_FlagCarrierBlue = protocol7::FLAG_ATSTAND;
			else if(m_apFlags[TEAM_BLUE]->m_pCarryingCharacter && m_apFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer())
				pFlag->m_FlagCarrierBlue = m_apFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer()->GetCID();
			else
				pFlag->m_FlagCarrierBlue = protocol7::FLAG_TAKEN;
		}
		else
			pFlag->m_FlagCarrierBlue = protocol7::FLAG_MISSING;
		return;
	}

	CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
	if(!pGameDataObj)
		return;

	pGameDataObj->m_TeamscoreRed = m_aTeamscore[TEAM_RED];
	pGameDataObj->m_TeamscoreBlue = m_aTeamscore[TEAM_BLUE];

	if(m_apFlags[TEAM_RED])
	{
		if(m_apFlags[TEAM_RED]->m_AtStand)
			pGameDataObj->m_FlagCarrierRed = FLAG_ATSTAND;
		else if(m_apFlags[TEAM_RED]->m_pCarryingCharacter && m_apFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer())
			pGameDataObj->m_FlagCarrierRed = m_apFlags[TEAM_RED]->m_pCarryingCharacter->GetPlayer()->GetCID();
		else
			pGameDataObj->m_FlagCarrierRed = FLAG_TAKEN;
	}
	else
		pGameDataObj->m_FlagCarrierRed = FLAG_MISSING;
	if(m_apFlags[TEAM_BLUE])
	{
		if(m_apFlags[TEAM_BLUE]->m_AtStand)
			pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
		else if(m_apFlags[TEAM_BLUE]->m_pCarryingCharacter && m_apFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer())
			pGameDataObj->m_FlagCarrierBlue = m_apFlags[TEAM_BLUE]->m_pCarryingCharacter->GetPlayer()->GetCID();
		else
			pGameDataObj->m_FlagCarrierBlue = FLAG_TAKEN;
	}
	else
		pGameDataObj->m_FlagCarrierBlue = FLAG_MISSING;
}

void CGameControllerOpenBattle::Tick()
{
	IGameController::Tick();

	if(GameServer()->m_World.m_ResetRequested)
		return;
	if(GameServer()->m_World.m_Paused)
	{
		if(m_GameOverTick != -1 && m_CurrentObjective != OBJECTIVE_NONE)
			EndObjective(false);
		return;
	}

	if(m_LastRoundStartTick != m_RoundStartTick)
	{
		m_LastRoundStartTick = m_RoundStartTick;
		ResetRoundState();
	}
	if(!m_MapScanned)
		ScanMapObjectives();

	m_RoundActiveTicks++;
	int TickSpeed = Server()->TickSpeed();
	if(m_aCheckpointAvailable[2] && !m_CUnlockWarningSent && m_RoundActiveTicks >= 15*TickSpeed)
	{
		m_CUnlockWarningSent = true;
		GameServer()->SendChatTarget(-1, "Checkpoint C unlocks in 5 seconds.");
		GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_PL);
	}
	if(m_aCheckpointAvailable[2] && !m_CUnlockedSent && m_RoundActiveTicks >= 20*TickSpeed)
	{
		m_CUnlockedSent = true;
		GameServer()->SendChatTarget(-1, "Checkpoint C is now open for capture!");
		GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
	}

	TickCheckpoints();
	if(m_SuddenDeath)
	{
		if(m_CurrentObjective != OBJECTIVE_NONE)
			EndObjective(true);
	}
	else
		TickDynamicObjectives();

	for(int fi = 0; fi < 2; fi++)
	{
		CFlag *F = m_apFlags[fi];

		if(!F)
			continue;

		// flag hits death-tile or left the game layer, reset it
		if(GameServer()->Collision()->GetCollisionAt(F->m_Pos.x, F->m_Pos.y)&CCollision::COLFLAG_DEATH || F->GameLayerClipped(F->m_Pos))
		{
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "flag_return");
			GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
			F->Reset();
			continue;
		}

		//
		if(F->m_pCarryingCharacter)
		{
			// update flag position
			F->m_Pos = F->m_pCarryingCharacter->m_Pos;

			if(m_apFlags[fi^1] && m_apFlags[fi^1]->m_AtStand)
			{
				if(distance(F->m_Pos, m_apFlags[fi^1]->m_Pos) < CFlag::ms_PhysSize + CCharacter::ms_PhysSize)
				{
					// CAPTURE! \o/
					int CapturingClient = F->m_pCarryingCharacter->GetPlayer()->GetCID();
					m_aTeamscore[fi^1] += 100;
					F->m_pCarryingCharacter->GetPlayer()->m_Score += 5;

					char aBuf[512];
					str_format(aBuf, sizeof(aBuf), "flag_capture player='%d:%s'",
						F->m_pCarryingCharacter->GetPlayer()->GetCID(),
						Server()->ClientName(F->m_pCarryingCharacter->GetPlayer()->GetCID()));
					GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

					float CaptureTime = (Server()->Tick() - F->m_GrabTick)/(float)Server()->TickSpeed();
					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(!GameServer()->m_apPlayers[i])
							continue;
						const char *pColor = GameServer()->Localize(fi ? "blue" : "red", i);
						if(CaptureTime <= 60)
							str_format(aBuf, sizeof(aBuf), GameServer()->Localize("The %s flag was captured by '%s' (%d.%s%d seconds)", i),
								pColor, Server()->ClientName(F->m_pCarryingCharacter->GetPlayer()->GetCID()),
								(int)CaptureTime%60, ((int)(CaptureTime*100)%100)<10?"0":"", (int)(CaptureTime*100)%100);
						else
							str_format(aBuf, sizeof(aBuf), GameServer()->Localize("The %s flag was captured by '%s'", i),
								pColor, Server()->ClientName(F->m_pCarryingCharacter->GetPlayer()->GetCID()));
						CNetMsg_Sv_Chat Msg;
						Msg.m_Team = 0;
						Msg.m_ClientID = -1;
						Msg.m_pMessage = aBuf;
						Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
					}
					OnFlagCaptured(fi^1, CapturingClient);
					for(int i = 0; i < 2; i++)
						m_apFlags[i]->Reset();

					GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
				}
			}
		}
		else
		{
			CCharacter *apCloseCCharacters[MAX_CLIENTS];
			int Num = GameServer()->m_World.FindEntities(F->m_Pos, CFlag::ms_PhysSize, (CEntity**)apCloseCCharacters, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
			for(int i = 0; i < Num; i++)
			{
				if(!apCloseCCharacters[i]->IsAlive() || apCloseCCharacters[i]->GetPlayer()->GetTeam() == TEAM_SPECTATORS || GameServer()->Collision()->IntersectLine(F->m_Pos, apCloseCCharacters[i]->m_Pos, NULL, NULL))
					continue;

				if(apCloseCCharacters[i]->GetPlayer()->GetTeam() == F->m_Team)
				{
					// return the flag
					if(!F->m_AtStand)
					{
						CCharacter *pChr = apCloseCCharacters[i];
						pChr->GetPlayer()->m_Score += 1;

						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "flag_return player='%d:%s'",
							pChr->GetPlayer()->GetCID(),
							Server()->ClientName(pChr->GetPlayer()->GetCID()));
						GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

						GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
						F->Reset();
					}
				}
				else
				{
					// take the flag
					if(F->m_AtStand)
					{
						m_aTeamscore[fi^1]++;
						F->m_GrabTick = Server()->Tick();
					}

					F->m_AtStand = 0;
					F->m_pCarryingCharacter = apCloseCCharacters[i];
					F->m_pCarryingCharacter->GetPlayer()->m_Score += 1;

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "flag_grab player='%d:%s'",
						F->m_pCarryingCharacter->GetPlayer()->GetCID(),
						Server()->ClientName(F->m_pCarryingCharacter->GetPlayer()->GetCID()));
					GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

					for(int c = 0; c < MAX_CLIENTS; c++)
					{
						CPlayer *pPlayer = GameServer()->m_apPlayers[c];
						if(!pPlayer)
							continue;

						if(pPlayer->GetTeam() == TEAM_SPECTATORS && pPlayer->m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[pPlayer->m_SpectatorID] && GameServer()->m_apPlayers[pPlayer->m_SpectatorID]->GetTeam() == fi)
							GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_EN, c);
						else if(pPlayer->GetTeam() == fi)
							GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_EN, c);
						else
							GameServer()->CreateSoundGlobal(SOUND_CTF_GRAB_PL, c);
					}
					break;
				}
			}

			if(!F->m_pCarryingCharacter && !F->m_AtStand)
			{
				if(Server()->Tick() > F->m_DropTick + Server()->TickSpeed()*30)
				{
					GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
					F->Reset();
				}
				else
				{
					F->m_Vel.y += GameServer()->m_World.m_Core.m_Tuning.m_Gravity;
					GameServer()->Collision()->MoveBox(&F->m_Pos, &F->m_Vel, vec2(F->ms_PhysSize, F->ms_PhysSize), 0.5f);
				}
			}
		}
	}
}

void CGameControllerOpenBattle::ResetObjectiveState()
{
	mem_zero(m_aObjectiveFirstReward, sizeof(m_aObjectiveFirstReward));
	mem_zero(m_aaObjectiveControlTicks, sizeof(m_aaObjectiveControlTicks));
	mem_zero(m_aObjectiveHoldTicks, sizeof(m_aObjectiveHoldTicks));
	mem_zero(m_aObjectiveCooldownTicks, sizeof(m_aObjectiveCooldownTicks));
	mem_zero(m_aObjectiveRewardCount, sizeof(m_aObjectiveRewardCount));
	mem_zero(m_aObjectiveAreaMask, sizeof(m_aObjectiveAreaMask));
	mem_zero(m_aObjectiveCompleted, sizeof(m_aObjectiveCompleted));
	mem_zero(m_aObjectiveStage, sizeof(m_aObjectiveStage));
}

bool CGameControllerOpenBattle::ObjectiveEligible(int Objective) const
{
	bool Flags = m_apFlags[TEAM_RED] && m_apFlags[TEAM_BLUE];
	switch(Objective)
	{
	case OBJECTIVE_FLAG_OFFENSIVE: return Flags;
	case OBJECTIVE_CENTRAL_DOMINATION: return m_aCheckpointAvailable[2];
	case OBJECTIVE_BEHIND_LINES: return m_aCheckpointAvailable[0] && m_aCheckpointAvailable[1];
	case OBJECTIVE_BREAKTHROUGH: return m_aCheckpointAvailable[0] && m_aCheckpointAvailable[1] && m_aCheckpointAvailable[2];
	case OBJECTIVE_AREA_SUPERIORITY:
		return (m_aCheckpointAvailable[0] ? 1 : 0)+(m_aCheckpointAvailable[1] ? 1 : 0)+(m_aCheckpointAvailable[2] ? 1 : 0) >= 2;
	case OBJECTIVE_COMBINED_ARMS: return Flags && m_aCheckpointAvailable[2];
	default: return false;
	}
}

int CGameControllerOpenBattle::ObjectiveDurationSeconds(int Objective) const
{
	return Objective == OBJECTIVE_BREAKTHROUGH || Objective == OBJECTIVE_COMBINED_ARMS ? 150 : 120;
}

const char *CGameControllerOpenBattle::ObjectiveName(int Objective) const
{
	switch(Objective)
	{
	case OBJECTIVE_FLAG_OFFENSIVE: return "Flag Offensive";
	case OBJECTIVE_CENTRAL_DOMINATION: return "Central Domination";
	case OBJECTIVE_BEHIND_LINES: return "Behind-the-Lines Raid";
	case OBJECTIVE_BREAKTHROUGH: return "Frontline Breakthrough";
	case OBJECTIVE_AREA_SUPERIORITY: return "Area Superiority";
	case OBJECTIVE_COMBINED_ARMS: return "Combined Arms";
	default: return "None";
	}
}

void CGameControllerOpenBattle::RefillObjectivePool()
{
	m_ObjectivePoolSize = 0;
	m_ObjectivePoolPos = 0;
	for(int Objective = 0; Objective < NUM_OBJECTIVES; Objective++)
		if(ObjectiveEligible(Objective))
			m_aObjectivePool[m_ObjectivePoolSize++] = Objective;
	for(int i = m_ObjectivePoolSize-1; i > 0; i--)
	{
		int j = rand()%(i+1);
		int Temp = m_aObjectivePool[i];
		m_aObjectivePool[i] = m_aObjectivePool[j];
		m_aObjectivePool[j] = Temp;
	}
	if(m_ObjectivePoolSize > 1 && m_aObjectivePool[0] == m_LastObjective)
	{
		int Temp = m_aObjectivePool[0];
		m_aObjectivePool[0] = m_aObjectivePool[1];
		m_aObjectivePool[1] = Temp;
	}
}

int CGameControllerOpenBattle::PickNextObjective()
{
	if(m_ObjectivePoolPos >= m_ObjectivePoolSize)
		RefillObjectivePool();
	return m_ObjectivePoolPos < m_ObjectivePoolSize ? m_aObjectivePool[m_ObjectivePoolPos++] : OBJECTIVE_NONE;
}

void CGameControllerOpenBattle::AnnounceObjective(const char *pFormat, int Objective, int Sound)
{
	char aBuf[256];
	for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
	{
		if(!GameServer()->m_apPlayers[ClientID])
			continue;
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize(pFormat, ClientID),
			GameServer()->Localize(ObjectiveName(Objective), ClientID));
		GameServer()->SendChatTarget(ClientID, aBuf);
	}
	if(Sound >= 0)
		GameServer()->CreateSoundGlobal(Sound);
}

void CGameControllerOpenBattle::AnnounceTeamCompletion(int Team)
{
	char aBuf[256];
	for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
	{
		if(!GameServer()->m_apPlayers[ClientID])
			continue;
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Dynamic objective completed by %s team: %s.", ClientID),
			GameServer()->Localize(Team == TEAM_RED ? "Red" : "Blue", ClientID),
			GameServer()->Localize(ObjectiveName(m_CurrentObjective), ClientID));
		GameServer()->SendChatTarget(ClientID, aBuf);
	}
	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
}

void CGameControllerOpenBattle::DetermineComebackTeam()
{
	m_ComebackTeam = -1;
	m_ComebackActive = false;
	if(!g_Config.m_SvDynamicComeback || m_SuddenDeath || m_aTeamscore[TEAM_RED] == m_aTeamscore[TEAM_BLUE])
		return;
	bool Late = false;
	if(g_Config.m_SvTimelimit > 0 && m_RoundActiveTicks >= g_Config.m_SvTimelimit*60*Server()->TickSpeed()*2/3)
		Late = true;
	int Leader = m_aTeamscore[TEAM_RED] > m_aTeamscore[TEAM_BLUE] ? TEAM_RED : TEAM_BLUE;
	if(g_Config.m_SvScorelimit > 0 && m_aTeamscore[Leader] >= g_Config.m_SvScorelimit*2/3)
		Late = true;
	if(g_Config.m_SvTimelimit == 0 && g_Config.m_SvScorelimit == 0 && m_RoundActiveTicks >= 10*60*Server()->TickSpeed())
		Late = true;
	int Difference = absolute(m_aTeamscore[TEAM_RED]-m_aTeamscore[TEAM_BLUE]);
	int Required = g_Config.m_SvScorelimit > 0 ? max(20, g_Config.m_SvScorelimit/5) : 100;
	if(Late && Difference >= Required)
	{
		m_ComebackTeam = Leader^1;
		m_ComebackActive = true;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "comeback enabled team='%d' difference='%d' objective='%s'", m_ComebackTeam, Difference, ObjectiveName(m_CurrentObjective));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "openbattle", aBuf);
	}
}

void CGameControllerOpenBattle::AddDynamicTeamScore(int Team, int Amount, const char *pReason)
{
	if(Team < TEAM_RED || Team > TEAM_BLUE || m_CurrentObjective == OBJECTIVE_NONE || m_SuddenDeath)
		return;
	if(m_ComebackActive && Team == m_ComebackTeam && m_aTeamscore[Team] >= m_aTeamscore[Team^1])
		m_ComebackActive = false;
	int Award = Amount;
	if(m_ComebackActive && Team == m_ComebackTeam)
		Award *= 2;
	m_aTeamscore[Team] += Award;
	if(m_ComebackActive && Team == m_ComebackTeam && m_aTeamscore[Team] >= m_aTeamscore[Team^1])
		m_ComebackActive = false;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "dynamic_score objective='%s' team='%d' base='%d' award='%d' reason='%s' comeback='%d'",
		ObjectiveName(m_CurrentObjective), Team, Amount, Award, pReason, Award != Amount);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "openbattle", aBuf);
}

void CGameControllerOpenBattle::StartObjective(int Objective)
{
	if(Objective == OBJECTIVE_NONE)
		return;
	m_CurrentObjective = Objective;
	m_ObjectiveElapsedTicks = 0;
	m_ObjectiveThirtySent = false;
	ResetObjectiveState();
	DetermineComebackTeam();
	AnnounceObjective("Dynamic objective started: %s.", Objective, SOUND_CTF_GRAB_PL);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "objective_start name='%s' duration='%d' comeback_team='%d'", ObjectiveName(Objective), ObjectiveDurationSeconds(Objective), m_ComebackActive ? m_ComebackTeam : -1);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "openbattle", aBuf);
}

void CGameControllerOpenBattle::EndObjective(bool SuddenDeath)
{
	if(m_CurrentObjective == OBJECTIVE_NONE)
		return;
	AnnounceObjective(SuddenDeath ? "Dynamic objective ended for sudden death: %s." : "Dynamic objective ended: %s.",
		m_CurrentObjective, SOUND_CTF_RETURN);
	m_LastObjective = m_CurrentObjective;
	m_CurrentObjective = OBJECTIVE_NONE;
	m_ComebackTeam = -1;
	m_ComebackActive = false;
	m_ObjectiveElapsedTicks = 0;
	ResetObjectiveState();
	if(!SuddenDeath)
	{
		m_NextObjectiveStartTick = m_RoundActiveTicks+60*Server()->TickSpeed();
		m_ObjectivePreannounced = false;
	}
}

void CGameControllerOpenBattle::TickDynamicObjectives()
{
	if(!g_Config.m_SvDynamicObjectives || m_GameOverTick != -1)
	{
		if(m_CurrentObjective != OBJECTIVE_NONE)
			EndObjective(false);
		return;
	}
	if(m_CurrentObjective == OBJECTIVE_NONE)
	{
		if(!m_ObjectivePreannounced && m_RoundActiveTicks >= m_NextObjectiveStartTick-15*Server()->TickSpeed())
		{
			int Next = PickNextObjective();
			if(Next == OBJECTIVE_NONE)
				return;
			// Reserve the announced objective at the front of a one-item pending slot.
			m_CurrentObjective = Next;
			m_ObjectivePreannounced = true;
			AnnounceObjective("Dynamic objective incoming: %s (starts in 15 seconds).", Next, SOUND_CHAT_HIGHLIGHT);
		}
		if(m_ObjectivePreannounced && m_RoundActiveTicks >= m_NextObjectiveStartTick)
		{
			int Objective = m_CurrentObjective;
			m_CurrentObjective = OBJECTIVE_NONE;
			m_ObjectivePreannounced = false;
			StartObjective(Objective);
		}
		return;
	}
	// A preannounced objective uses m_CurrentObjective but has not started yet.
	if(m_ObjectivePreannounced)
		return;

	if(m_ComebackActive && m_ComebackTeam >= 0 && m_aTeamscore[m_ComebackTeam] >= m_aTeamscore[m_ComebackTeam^1])
	{
		m_ComebackActive = false;
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "openbattle", "comeback disabled: trailing team caught up");
	}
	m_ObjectiveElapsedTicks++;
	TickObjectiveRules();
	int DurationTicks = ObjectiveDurationSeconds(m_CurrentObjective)*Server()->TickSpeed();
	if(!m_ObjectiveThirtySent && m_ObjectiveElapsedTicks >= DurationTicks-30*Server()->TickSpeed())
	{
		m_ObjectiveThirtySent = true;
		AnnounceObjective("Dynamic objective: %s — 30 seconds remaining.", m_CurrentObjective, SOUND_CHAT_HIGHLIGHT);
	}
	if(m_ObjectiveElapsedTicks >= DurationTicks)
		EndObjective(false);
}

void CGameControllerOpenBattle::TickObjectiveRules()
{
	int TickSpeed = Server()->TickSpeed();
	for(int Team = TEAM_RED; Team <= TEAM_BLUE; Team++)
	{
		if(m_CurrentObjective == OBJECTIVE_CENTRAL_DOMINATION)
		{
			if(CheckpointControlled(2, Team))
			{
				if(++m_aaObjectiveControlTicks[Team][2] >= 10*TickSpeed)
				{
					m_aaObjectiveControlTicks[Team][2] = 0;
					AddDynamicTeamScore(Team, 5, "central_control");
				}
			}
			else m_aaObjectiveControlTicks[Team][2] = 0;
		}
		else if(m_CurrentObjective == OBJECTIVE_BEHIND_LINES)
		{
			int Target = Team == TEAM_RED ? 1 : 0;
			if(CheckpointControlled(Target, Team))
			{
				if(++m_aaObjectiveControlTicks[Team][Target] >= 15*TickSpeed)
				{
					m_aaObjectiveControlTicks[Team][Target] = 0;
					AddDynamicTeamScore(Team, 5, "enemy_checkpoint_control");
				}
			}
			else m_aaObjectiveControlTicks[Team][Target] = 0;
		}
		else if(m_CurrentObjective == OBJECTIVE_BREAKTHROUGH && !m_aObjectiveCompleted[Team])
		{
			int EnemyPoint = Team == TEAM_RED ? 1 : 0;
			bool HasC = CheckpointControlled(2, Team);
			bool HasBoth = HasC && CheckpointControlled(EnemyPoint, Team);
			m_aObjectiveStage[Team] = !HasC ? 0 : (HasBoth ? 2 : 1);
			if(HasBoth)
			{
				if(++m_aObjectiveHoldTicks[Team] >= 10*TickSpeed)
				{
					m_aObjectiveCompleted[Team] = true;
					m_aObjectiveStage[Team] = 3;
					AddDynamicTeamScore(Team, 50, "breakthrough_complete");
					AnnounceTeamCompletion(Team);
				}
			}
			else m_aObjectiveHoldTicks[Team] = 0;
		}
		else if(m_CurrentObjective == OBJECTIVE_AREA_SUPERIORITY && m_aObjectiveRewardCount[Team] < 2)
		{
			if(m_aObjectiveCooldownTicks[Team] > 0)
			{
				m_aObjectiveCooldownTicks[Team]--;
				m_aObjectiveHoldTicks[Team] = 0;
				continue;
			}
			int Controlled = 0;
			int OwnedMask = 0;
			for(int Point = 0; Point < 3; Point++)
				if(m_aCheckpointAvailable[Point] && CheckpointControlled(Point, Team))
				{
					Controlled++;
					OwnedMask |= 1<<Point;
				}
			if(Controlled >= 2)
			{
				if(m_aObjectiveAreaMask[Team] != 0 && (OwnedMask&m_aObjectiveAreaMask[Team]) != m_aObjectiveAreaMask[Team])
					m_aObjectiveHoldTicks[Team] = 0;
				m_aObjectiveAreaMask[Team] = OwnedMask;
				if(++m_aObjectiveHoldTicks[Team] >= 15*TickSpeed)
				{
					m_aObjectiveHoldTicks[Team] = 0;
					m_aObjectiveCooldownTicks[Team] = 30*TickSpeed;
					m_aObjectiveRewardCount[Team]++;
					AddDynamicTeamScore(Team, 25, "area_superiority");
					AnnounceTeamCompletion(Team);
				}
			}
			else
			{
				m_aObjectiveHoldTicks[Team] = 0;
				m_aObjectiveAreaMask[Team] = 0;
			}
		}
	}
}

void CGameControllerOpenBattle::OnFlagCaptured(int Team, int ClientID)
{
	if(m_CurrentObjective == OBJECTIVE_NONE || m_ObjectivePreannounced || m_SuddenDeath)
		return;
	if(m_CurrentObjective == OBJECTIVE_FLAG_OFFENSIVE)
	{
		AddDynamicTeamScore(Team, 50, "flag_offensive_capture");
		if(ClientID >= 0 && GameServer()->m_apPlayers[ClientID])
			GameServer()->m_apPlayers[ClientID]->m_Score += 2;
		AnnounceTeamCompletion(Team);
	}
	else if(m_CurrentObjective == OBJECTIVE_COMBINED_ARMS && CheckpointControlled(2, Team))
	{
		AddDynamicTeamScore(Team, 50, "combined_arms_capture");
		if(ClientID >= 0 && GameServer()->m_apPlayers[ClientID])
			GameServer()->m_apPlayers[ClientID]->m_Score += 3;
		AnnounceTeamCompletion(Team);
	}
}

void CGameControllerOpenBattle::SendObjectiveStatus(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !GameServer()->m_apPlayers[ClientID])
		return;
	char aBuf[256];
	if(!g_Config.m_SvDynamicObjectives)
	{
		GameServer()->SendChatTarget(ClientID, "Dynamic objectives are disabled.");
		return;
	}
	if(m_CurrentObjective == OBJECTIVE_NONE)
	{
		int Seconds = max(0, (m_NextObjectiveStartTick-m_RoundActiveTicks+Server()->TickSpeed()-1)/Server()->TickSpeed());
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("No active objective. Next objective in %d seconds.", ClientID), Seconds);
		GameServer()->SendChatTarget(ClientID, aBuf);
		return;
	}
	if(m_ObjectivePreannounced)
	{
		int Seconds = max(0, (m_NextObjectiveStartTick-m_RoundActiveTicks+Server()->TickSpeed()-1)/Server()->TickSpeed());
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Incoming objective: %s (%d seconds).", ClientID),
			GameServer()->Localize(ObjectiveName(m_CurrentObjective), ClientID), Seconds);
		GameServer()->SendChatTarget(ClientID, aBuf);
		return;
	}
	int Remaining = max(0, ObjectiveDurationSeconds(m_CurrentObjective)-m_ObjectiveElapsedTicks/Server()->TickSpeed());
	str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Current objective: %s (%d seconds remaining).", ClientID),
		GameServer()->Localize(ObjectiveName(m_CurrentObjective), ClientID), Remaining);
	GameServer()->SendChatTarget(ClientID, aBuf);
	int Team = GameServer()->m_apPlayers[ClientID]->GetTeam();
	if(Team == TEAM_RED || Team == TEAM_BLUE)
	{
		if(m_CurrentObjective == OBJECTIVE_FLAG_OFFENSIVE)
			GameServer()->SendChatTarget(ClientID, "Team task: capture the enemy flag and return it to base.");
		else if(m_CurrentObjective == OBJECTIVE_CENTRAL_DOMINATION)
		{
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Team task: control C; continuous-control progress %d/10 seconds.", ClientID), m_aaObjectiveControlTicks[Team][2]/Server()->TickSpeed());
			GameServer()->SendChatTarget(ClientID, aBuf);
		}
		else if(m_CurrentObjective == OBJECTIVE_BEHIND_LINES)
		{
			int Target = Team == TEAM_RED ? 1 : 0;
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Team task: capture enemy checkpoint %c; control progress %d/15 seconds.", ClientID), 'A'+Target, m_aaObjectiveControlTicks[Team][Target]/Server()->TickSpeed());
			GameServer()->SendChatTarget(ClientID, aBuf);
		}
		else if(m_CurrentObjective == OBJECTIVE_BREAKTHROUGH)
		{
			const char *pStage = m_aObjectiveStage[Team] == 0 ? "capture C" : (m_aObjectiveStage[Team] == 1 ? "capture the enemy near checkpoint while holding C" : (m_aObjectiveStage[Team] == 2 ? "hold both checkpoints" : "completed"));
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Team task: %s; hold progress %d/10 seconds.", ClientID),
				GameServer()->Localize(pStage, ClientID), m_aObjectiveHoldTicks[Team]/Server()->TickSpeed());
			GameServer()->SendChatTarget(ClientID, aBuf);
		}
		else if(m_CurrentObjective == OBJECTIVE_COMBINED_ARMS)
			GameServer()->SendChatTarget(ClientID, "Team task: control C when your team completes a flag capture.");
		else if(m_CurrentObjective == OBJECTIVE_AREA_SUPERIORITY)
		{
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Team task: hold any two checkpoints; progress %d/15, rewards %d/2.", ClientID),
				m_aObjectiveHoldTicks[Team]/Server()->TickSpeed(), m_aObjectiveRewardCount[Team]);
			GameServer()->SendChatTarget(ClientID, aBuf);
		}
		if(m_ComebackActive && Team == m_ComebackTeam)
			GameServer()->SendChatTarget(ClientID, "Comeback bonus active: dynamic team rewards are doubled until your team catches up.");
		else
			GameServer()->SendChatTarget(ClientID, "Comeback bonus is not active for your team.");
	}
}
