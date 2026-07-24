/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include <engine/map.h>
#include <engine/console.h>
#include <engine/localization.h>
#include <engine/storage.h>
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/generated/protocolglue.h>
#include "gamemodes/openbattle.h"
#include "entities/supply_station.h"

enum
{
	RESET,
	NO_RESET
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;
	m_pLocalization = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_BattlefieldWaterEnabled = false;
	for(int i = 0; i < 3; i++)
	{
		m_aCheckpointState[i] = 0;
		m_aCheckpointCaptured[i] = false;
		m_aCheckpointWarning[i][0] = false;
		m_aCheckpointWarning[i][1] = false;
		m_aCheckpointProgressSoundCooldown[i] = 0;
		m_aCheckpointDestination[i] = vec2(0, 0);
	}
	m_aCheckpointWarningCooldown[0] = 0;
	m_aCheckpointWarningCooldown[1] = 0;
	for(int i = 0; i < 6; i++)
	{
		m_aDoorState[i] = 0;
		m_aDoorStart[i] = vec2(0, 0);
		m_aDoorEnd[i] = vec2(0, 0);
	}
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_SixupCallVoteForce = false;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
}


class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

bool CGameContext::IntersectBattlefieldDoor(vec2 From, vec2 To, vec2 *pHit, float Radius)
{
	// Sample the path at one-unit steps against all six door segments so the
	// first contact point is returned, not merely the closest point.
	float PathLength = distance(From, To);
	int Steps = max(1, (int)ceilf(PathLength));
	for(int Step = 0; Step <= Steps; Step++)
	{
		float Amount = Step/(float)Steps;
		vec2 Point = From+(To-From)*Amount;
		for(int Door = 0; Door < 6; Door++)
		{
			if(m_aDoorState[Door] != 1)
				continue;
			vec2 Closest = m_aDoorStart[Door];
			if(distance(m_aDoorStart[Door], m_aDoorEnd[Door]) > 0.001f)
				Closest = closest_point_on_line(m_aDoorStart[Door], m_aDoorEnd[Door], Point);
			if(distance(Point, Closest) < Radius)
			{
				if(pHit)
					*pHit = Point;
				return true;
			}
		}
	}
	return false;
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount)
{
	float a = 3 * 3.14159f / 2 + Angle;
	//float a = get_angle(dir);
	float s = a-pi/3;
	float e = a+pi/3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i+1)/float(Amount+2));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd));
		if(pEvent)
		{
			pEvent->m_X = round_to_int(Pos.x);
			pEvent->m_Y = round_to_int(Pos.y);
			pEvent->m_Angle = round_to_int(f*256.0f);
		}
	}
}

void CGameContext::CreateDamageIndd(vec2 Pos, float Angle, int Amount)
{
	float Center = 3.0f*pi/2.0f+Angle;
	float Start = Center-pi;
	float End = Center+pi;
	for(int i = 0; i < Amount; i++)
	{
		float EventAngle = mix(Start, End, float(i+1)/float(Amount));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(
			NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd));
		if(pEvent)
		{
			pEvent->m_X = round_to_int(Pos.x);
			pEvent->m_Y = round_to_int(Pos.y);
			pEvent->m_Angle = round_to_int(EventAngle*256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = round_to_int(Pos.x);
		pEvent->m_Y = round_to_int(Pos.y);
	}
}


void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = round_to_int(Pos.x);
		pEvent->m_Y = round_to_int(Pos.y);
	}

	if (!NoDamage)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			if(l)
				ForceDir = normalize(Diff);
			l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Dmg = 6 * l;
			int DamageAmount = round_to_int(Dmg);
			if(DamageAmount)
				apEnts[i]->TakeDamage(ForceDir*Dmg*2, DamageAmount, Owner, Weapon);
		}
	}
}

void CGameContext::CreateExplosion2(vec2 Pos, int Owner, int Weapon, int Damage)
{
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(
		NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = round_to_int(Pos.x);
		pEvent->m_Y = round_to_int(Pos.y);
	}

	if(Damage == 1)
	{
		CCharacter *apEnts[MAX_CLIENTS];
		const float Radius = 135.0f;
		const float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity **)apEnts,
			MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos-Pos;
			vec2 ForceDir(0, 1);
			float Len = length(Diff);
			if(Len)
				ForceDir = normalize(Diff);
			float Scale = 1.0f-clamp((Len-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Dmg = 3.0f*Scale;
			int DamageAmount = round_to_int(Dmg);
			if(DamageAmount)
				apEnts[i]->TakeDamage(ForceDir*Dmg*2.0f, DamageAmount, Owner, Weapon);
		}
	}
}

/*
void create_smoke(vec2 Pos)
{
	// create the event
	EV_EXPLOSION *pEvent = (EV_EXPLOSION *)events.create(EVENT_SMOKE, sizeof(EV_EXPLOSION));
	if(pEvent)
	{
		pEvent->x = (int)Pos.x;
		pEvent->y = (int)Pos.y;
	}
}*/

void CGameContext::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = round_to_int(Pos.x);
		ev->m_Y = round_to_int(Pos.y);
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = round_to_int(Pos.x);
		pEvent->m_Y = round_to_int(Pos.y);
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = round_to_int(Pos.x);
		pEvent->m_Y = round_to_int(Pos.y);
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if(Sound < 0)
		return;

	if(Target == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && Server()->ClientIngame(i))
				CreateSoundGlobal(Sound, i);
		}
		return;
	}

	if(Server()->IsSixup(Target))
	{
		// 0.7 has no Sv_SoundGlobal — play at viewer position
		if(m_apPlayers[Target])
			CreateSound(m_apPlayers[Target]->m_ViewPos, Sound, CmaskOne(Target));
		return;
	}

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, Target);
}


void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;

	if(To < 0)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!m_apPlayers[i])
				continue;
			Msg.m_pMessage = Localize(pText, i);
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
		return;
	}

	Msg.m_pMessage = Localize(pText, To);
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
}


void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);

	if(Team == CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;
		// System messages (ClientID < 0): localize per recipient
		if(ChatterClientID < 0)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!m_apPlayers[i])
					continue;
				Msg.m_pMessage = Localize(pText, i);
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
			}
		}
		else
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == Team)
			{
				if(ChatterClientID < 0)
					Msg.m_pMessage = Localize(pText, i);
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
			}
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	CCharacter *pCharacter = GetPlayerChar(ClientID);
	if(pCharacter)
		pCharacter->HandleBattlefieldExitCommand();
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}


void CGameContext::SendBroadcast(const char *pText, int ClientID)
{
	CNetMsg_Sv_Broadcast Msg;
	if(ClientID < 0)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!m_apPlayers[i])
				continue;
			Msg.m_pMessage = Localize(pText, i);
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
		return;
	}

	Msg.m_pMessage = Localize(pText, ClientID);
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*25;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(-1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote()
{
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
{
	CNetMsg_Sv_VoteSet Msg6;
	protocol7::CNetMsg_Sv_VoteSet Msg7;
	Msg7.m_ClientID = m_VoteCreator;

	if(m_VoteCloseTime)
	{
		Msg6.m_Timeout = Msg7.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg6.m_pDescription = Msg7.m_pDescription = m_aVoteDescription;
		Msg6.m_pReason = Msg7.m_pReason = m_aVoteReason;
		Msg7.m_Type = protocol7::VOTE_START_OP;
		if(!str_comp_num(m_aVoteCommand, "kick ", 5))
			Msg7.m_Type = protocol7::VOTE_START_KICK;
		else if(!str_comp_num(m_aVoteCommand, "set_team ", 9))
			Msg7.m_Type = protocol7::VOTE_START_SPEC;
	}
	else
	{
		Msg6.m_Timeout = Msg7.m_Timeout = 0;
		Msg6.m_pDescription = Msg7.m_pDescription = "";
		Msg6.m_pReason = Msg7.m_pReason = "";
		if(m_VoteEnforce == VOTE_ENFORCE_YES)
			Msg7.m_Type = protocol7::VOTE_END_PASS;
		else if(m_VoteEnforce == VOTE_ENFORCE_NO)
			Msg7.m_Type = protocol7::VOTE_END_FAIL;
		else
			Msg7.m_Type = protocol7::VOTE_END_ABORT;
	}

	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!m_apPlayers[i])
				continue;
			if(Server()->IsSixup(i))
				Server()->SendPackMsg(&Msg7, MSGFLAG_VITAL, i);
			else
				Server()->SendPackMsg(&Msg6, MSGFLAG_VITAL, i);
		}
	}
	else if(Server()->IsSixup(ClientID))
		Server()->SendPackMsg(&Msg7, MSGFLAG_VITAL, ClientID);
	else
		Server()->SendPackMsg(&Msg6, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

}

void CGameContext::AbortVoteKickOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientID) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientID)))
		m_VoteCloseTime = -1;
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->m_pGameType, "DM")==0 ||
		str_comp(m_pController->m_pGameType, "TDM")==0 ||
		str_comp(m_pController->m_pGameType, "CTF")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CheckPureTuning();

	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && Server()->ClientIngame(i))
				SendTuningParams(i);
		}
		return;
	}

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	unsigned Num = sizeof(m_Tuning)/sizeof(int);
	for(unsigned i = 0; i < Num; i++)
	{
		// laser_damage removed in 0.7 — skipping keeps collision/hooking aligned
		if(Server()->IsSixup(ClientID) && i == 30)
			continue;
		Msg.AddInt(pParams[i]);
	}
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::OnTick()
{
	// check tuning
	CheckPureTuning();

	for(int i = 0; i < 2; i++)
	{
		if(m_aCheckpointWarningCooldown[i] > 0)
			m_aCheckpointWarningCooldown[i]--;
	}
	for(int i = 0; i < 3; i++)
	{
		if(m_aCheckpointProgressSoundCooldown[i] > 0)
			m_aCheckpointProgressSoundCooldown[i]--;
	}

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
		}
	}

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
			EndVote();
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i])	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}

				if(Yes >= Total/2+1)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= (Total+1)/2)
					m_VoteEnforce = VOTE_ENFORCE_NO;
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				Console()->ExecuteLine(m_aVoteCommand);
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote passed");

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote failed");
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}


#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[MAX_CLIENTS-i-1]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::SendSixupGameInfo(int ClientID)
{
	if(!Server()->IsSixup(ClientID) || !m_pController)
		return;

	protocol7::CNetMsg_Sv_GameInfo Msg;
	Msg.m_GameFlags = protocol7::GAMEFLAG_TEAMS | protocol7::GAMEFLAG_FLAGS;
	Msg.m_ScoreLimit = g_Config.m_SvScorelimit;
	Msg.m_TimeLimit = g_Config.m_SvTimelimit;
	Msg.m_MatchNum = (str_length(g_Config.m_SvMaprotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;
	Msg.m_MatchCurrent = 1;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
}

void CGameContext::SendSixupServerSettings(int ClientID)
{
	if(!Server()->IsSixup(ClientID))
		return;

	protocol7::CNetMsg_Sv_ServerSettings Msg;
	Msg.m_KickVote = g_Config.m_SvVoteKick;
	Msg.m_KickMin = g_Config.m_SvVoteKickMin;
	Msg.m_SpecVote = g_Config.m_SvVoteSpectate;
	Msg.m_TeamLock = 0;
	Msg.m_TeamBalance = g_Config.m_SvTeambalanceTime != 0;
	Msg.m_PlayerSlots = g_Config.m_SvMaxClients - g_Config.m_SvSpectatorSlots;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
}

void CGameContext::SendSixupSkinChange(int ClientID)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if(!pPlayer)
		return;

	protocol7::CNetMsg_Sv_SkinChange Msg;
	Msg.m_ClientID = ClientID;
	for(int p = 0; p < 6; p++)
	{
		Msg.m_apSkinPartNames[p] = pPlayer->m_TeeInfos.m_aaSkinPartNames[p];
		Msg.m_aUseCustomColors[p] = pPlayer->m_TeeInfos.m_aUseCustomColors[p];
		Msg.m_aSkinPartColors[p] = pPlayer->m_TeeInfos.m_aSkinPartColors[p];
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);
}

void CGameContext::SendSixupClientDrop(int ClientID, const char *pReason, bool Silent)
{
	protocol7::CNetMsg_Sv_ClientDrop Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_pReason = pReason ? pReason : "";
	Msg.m_Silent = Silent ? 1 : 0;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);
}

void CGameContext::SendSixupTeam(int ClientID, bool Silent)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if(!pPlayer)
		return;

	protocol7::CNetMsg_Sv_Team Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Team = pPlayer->GetTeam();
	Msg.m_Silent = Silent ? 1 : 0;
	Msg.m_CooldownTick = pPlayer->m_LastSetTeam + Server()->TickSpeed() * 3;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);
}

void CGameContext::SendSixupClientInfos(int ClientID)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if(!pPlayer)
		return;

	protocol7::CNetMsg_Sv_ClientInfo NewInfo;
	NewInfo.m_ClientID = ClientID;
	NewInfo.m_Local = 0;
	NewInfo.m_Team = pPlayer->GetTeam();
	NewInfo.m_pName = Server()->ClientName(ClientID);
	NewInfo.m_Country = Server()->ClientCountry(ClientID);
	NewInfo.m_Silent = 1;
	for(int p = 0; p < 6; p++)
	{
		NewInfo.m_apSkinPartNames[p] = pPlayer->m_TeeInfos.m_aaSkinPartNames[p];
		NewInfo.m_aUseCustomColors[p] = pPlayer->m_TeeInfos.m_aUseCustomColors[p];
		NewInfo.m_aSkinPartColors[p] = pPlayer->m_TeeInfos.m_aSkinPartColors[p];
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == ClientID || !m_apPlayers[i] || !Server()->ClientIngame(i))
			continue;

		if(Server()->IsSixup(i))
		{
			NewInfo.m_pClan = pPlayer->GetSnapClan(i);
			Server()->SendPackMsg(&NewInfo, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
		}

		if(Server()->IsSixup(ClientID))
		{
			protocol7::CNetMsg_Sv_ClientInfo Info;
			Info.m_ClientID = i;
			Info.m_Local = 0;
			Info.m_Team = m_apPlayers[i]->GetTeam();
			Info.m_pName = Server()->ClientName(i);
			Info.m_pClan = m_apPlayers[i]->GetSnapClan(ClientID);
			Info.m_Country = Server()->ClientCountry(i);
			Info.m_Silent = 1;
			for(int p = 0; p < 6; p++)
			{
				Info.m_apSkinPartNames[p] = m_apPlayers[i]->m_TeeInfos.m_aaSkinPartNames[p];
				Info.m_aUseCustomColors[p] = m_apPlayers[i]->m_TeeInfos.m_aUseCustomColors[p];
				Info.m_aSkinPartColors[p] = m_apPlayers[i]->m_TeeInfos.m_aSkinPartColors[p];
			}
			Server()->SendPackMsg(&Info, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
		}
	}

	if(Server()->IsSixup(ClientID))
	{
		NewInfo.m_Local = 1;
		NewInfo.m_pClan = pPlayer->GetSnapClan(ClientID);
		Server()->SendPackMsg(&NewInfo, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
		SendSixupGameInfo(ClientID);
		SendSixupServerSettings(ClientID);
	}
}

void CGameContext::SendSixupClientInfoUpdate(int ClientID)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if(!pPlayer || !Server()->ClientIngame(ClientID))
		return;

	protocol7::CNetMsg_Sv_ClientInfo Info;
	Info.m_ClientID = ClientID;
	Info.m_Team = pPlayer->GetTeam();
	Info.m_pName = Server()->ClientName(ClientID);
	Info.m_Country = Server()->ClientCountry(ClientID);
	Info.m_Silent = 1;
	for(int p = 0; p < 6; p++)
	{
		Info.m_apSkinPartNames[p] = pPlayer->m_TeeInfos.m_aaSkinPartNames[p];
		Info.m_aUseCustomColors[p] = pPlayer->m_TeeInfos.m_aUseCustomColors[p];
		Info.m_aSkinPartColors[p] = pPlayer->m_TeeInfos.m_aSkinPartColors[p];
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!m_apPlayers[i] || !Server()->ClientIngame(i) || !Server()->IsSixup(i))
			continue;
		Info.m_Local = (i == ClientID);
		Info.m_pClan = pPlayer->GetSnapClan(i);
		Server()->SendPackMsg(&Info, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
	}
}

void CGameContext::OnClientEnter(int ClientID)
{
	//world.insert_entity(&players[client_id]);
	m_apPlayers[ClientID]->Respawn();
	SendSixupClientInfos(ClientID);

	char aBuf[512];
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!m_apPlayers[i])
			continue;
		str_format(aBuf, sizeof(aBuf), Localize("'%s' entered and joined the %s", i),
			Server()->ClientName(ClientID),
			Localize(m_pController->GetTeamName(m_apPlayers[ClientID]->GetTeam()), i));
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = -1;
		Msg.m_pMessage = aBuf;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}

	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), m_apPlayers[ClientID]->GetTeam());
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	SendWelcomeTutorial(ClientID);
	m_VoteUpdate = true;
}

void CGameContext::OnClientConnected(int ClientID)
{
	// Check which team the player should be on
	const int StartTeam = g_Config.m_SvTournamentMode ? TEAM_SPECTATORS : m_pController->GetAutoTeam(ClientID);

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
	//players[client_id].init(client_id);
	//players[client_id].client_id = client_id;

	(void)m_pController->CheckTeamBalance();

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		if(ClientID >= MAX_CLIENTS-g_Config.m_DbgDummies)
			return;
	}
#endif

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	// send motd
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = g_Config.m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
	// Re-entrant Drop during leave broadcasts — player already torn down.
	if(!m_apPlayers[ClientID])
		return;

	AbortVoteKickOnDisconnect(ClientID);
	m_apPlayers[ClientID]->OnDisconnect(pReason);
	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	SendSixupClientDrop(ClientID, pReason, false);

	(void)m_pController->CheckTeamBalance();
	m_VoteUpdate = true;

	// update spectator modes
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_SpectatorID == ClientID)
			m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
	}
}

void *CGameContext::PreProcessMsg(int *pMsgID, CUnpacker *pUnpacker, int ClientID)
{
	if(Server()->IsSixup(ClientID))
	{
		void *pRawMsg = m_NetObjHandler7.SecureUnpackMsg(*pMsgID, pUnpacker);
		if(!pRawMsg)
			return 0;

		CPlayer *pPlayer = m_apPlayers[ClientID];
		static char s_aRawMsg[1024];

		if(*pMsgID == protocol7::NETMSGTYPE_CL_SAY)
		{
			protocol7::CNetMsg_Cl_Say *pMsg7 = (protocol7::CNetMsg_Cl_Say *)pRawMsg;
			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)s_aRawMsg;
			if(pMsg7->m_Target >= 0)
				return 0; // whisper not supported in OB sixup v1
			pMsg->m_Team = pMsg7->m_Mode == protocol7::CHAT_TEAM;
			pMsg->m_pMessage = pMsg7->m_pMessage;
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_STARTINFO)
		{
			protocol7::CNetMsg_Cl_StartInfo *pMsg7 = (protocol7::CNetMsg_Cl_StartInfo *)pRawMsg;
			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)s_aRawMsg;
			pMsg->m_pName = pMsg7->m_pName;
			pMsg->m_pClan = pMsg7->m_pClan;
			pMsg->m_Country = pMsg7->m_Country;
			str_copy(s_aRawMsg + sizeof(*pMsg), "default", sizeof(s_aRawMsg) - sizeof(*pMsg));
			pMsg->m_pSkin = s_aRawMsg + sizeof(*pMsg);
			pMsg->m_UseCustomColor = pMsg7->m_aUseCustomColors[0];
			pMsg->m_ColorBody = pMsg7->m_aSkinPartColors[0];
			pMsg->m_ColorFeet = pMsg7->m_aSkinPartColors[4];
			if(pPlayer)
			{
				for(int p = 0; p < 6; p++)
				{
					str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg7->m_apSkinPartNames[p], sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[p]));
					pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg7->m_aUseCustomColors[p];
					pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg7->m_aSkinPartColors[p];
				}
			}
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_SETSPECTATORMODE)
		{
			protocol7::CNetMsg_Cl_SetSpectatorMode *pMsg7 = (protocol7::CNetMsg_Cl_SetSpectatorMode *)pRawMsg;
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)s_aRawMsg;
			if(pMsg7->m_SpecMode == protocol7::SPEC_FREEVIEW)
				pMsg->m_SpectatorID = SPEC_FREEVIEW;
			else
				pMsg->m_SpectatorID = pMsg7->m_SpectatorID;
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_SETTEAM)
		{
			protocol7::CNetMsg_Cl_SetTeam *pMsg7 = (protocol7::CNetMsg_Cl_SetTeam *)pRawMsg;
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)s_aRawMsg;
			pMsg->m_Team = pMsg7->m_Team;
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_EMOTICON)
		{
			protocol7::CNetMsg_Cl_Emoticon *pMsg7 = (protocol7::CNetMsg_Cl_Emoticon *)pRawMsg;
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)s_aRawMsg;
			pMsg->m_Emoticon = pMsg7->m_Emoticon;
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_VOTE)
		{
			protocol7::CNetMsg_Cl_Vote *pMsg7 = (protocol7::CNetMsg_Cl_Vote *)pRawMsg;
			CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)s_aRawMsg;
			pMsg->m_Vote = pMsg7->m_Vote;
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_CALLVOTE)
		{
			protocol7::CNetMsg_Cl_CallVote *pMsg7 = (protocol7::CNetMsg_Cl_CallVote *)pRawMsg;
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)s_aRawMsg;
			pMsg->m_Type = pMsg7->m_Type;
			pMsg->m_Value = pMsg7->m_Value;
			pMsg->m_Reason = pMsg7->m_Reason;
			m_SixupCallVoteForce = pMsg7->m_Force && Server()->IsAuthed(ClientID);
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_SKINCHANGE)
		{
			protocol7::CNetMsg_Cl_SkinChange *pMsg7 = (protocol7::CNetMsg_Cl_SkinChange *)pRawMsg;
			if(pPlayer)
			{
				if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*5 > Server()->Tick())
					return 0;
				pPlayer->m_LastChangeInfo = Server()->Tick();
				for(int p = 0; p < 6; p++)
				{
					str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg7->m_apSkinPartNames[p], sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[p]));
					pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg7->m_aUseCustomColors[p];
					pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg7->m_aSkinPartColors[p];
				}
				pPlayer->m_TeeInfos.m_UseCustomColor = pMsg7->m_aUseCustomColors[0];
				pPlayer->m_TeeInfos.m_ColorBody = pMsg7->m_aSkinPartColors[0];
				pPlayer->m_TeeInfos.m_ColorFeet = pMsg7->m_aSkinPartColors[4];
				m_pController->OnPlayerInfoChange(pPlayer);
				SendSixupSkinChange(ClientID);
			}
			return 0;
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_COMMAND)
		{
			protocol7::CNetMsg_Cl_Command *pMsg7 = (protocol7::CNetMsg_Cl_Command *)pRawMsg;
			char aCmd[256];
			if(pMsg7->m_Arguments && pMsg7->m_Arguments[0])
				str_format(aCmd, sizeof(aCmd), "/%s %s", pMsg7->m_Name, pMsg7->m_Arguments);
			else
				str_format(aCmd, sizeof(aCmd), "/%s", pMsg7->m_Name);
			HandleChatCommand(ClientID, aCmd);
			return 0;
		}
		else if(*pMsgID == protocol7::NETMSGTYPE_CL_KILL)
		{
			// empty payload — fall through with dummy buffer
		}
		else
			return 0;

		*pMsgID = Msg_SevenToSix(*pMsgID);
		if(*pMsgID < 0)
			return 0;
		return s_aRawMsg;
	}
	return m_NetObjHandler.SecureUnpackMsg(*pMsgID, pUnpacker);
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = PreProcessMsg(&MsgID, pUnpacker, ClientID);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(MsgID == NETMSGTYPE_CL_SAY)
	{
		CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;
		int Team = pMsg->m_Team;
		if(Team)
			Team = pPlayer->GetTeam();
		else
			Team = CGameContext::CHAT_ALL;

		if(g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed() > Server()->Tick())
			return;

		if(pMsg->m_pMessage[0] == '/')
		{
			if(HandleChatCommand(ClientID, pMsg->m_pMessage))
				return;
			SendChatTarget(ClientID, "no such command");
			SendChatTarget(ClientID, "try /cmds");
			return;
		}
		if(pMsg->m_pMessage[0] == '\0')
			return;

		pPlayer->m_LastChat = Server()->Tick();

		// check for invalid chars
		unsigned char *pMessage = (unsigned char *)pMsg->m_pMessage;
		while (*pMessage)
		{
			if(*pMessage < 32)
				*pMessage = ' ';
			pMessage++;
		}

		SendChat(ClientID, Team, pMsg->m_pMessage);
	}
	else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
	{
		if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
			return;

		int64 Now = Server()->Tick();
		pPlayer->m_LastVoteTry = Now;
		if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		{
			SendChatTarget(ClientID, "Spectators aren't allowed to start a vote.");
			return;
		}

		if(m_VoteCloseTime)
		{
			SendChatTarget(ClientID, "Wait for current vote to end before calling a new one.");
			return;
		}

		int Timeleft = pPlayer->m_LastVoteCall + Server()->TickSpeed()*60 - Now;
		if(pPlayer->m_LastVoteCall && Timeleft > 0)
		{
			char aChatmsg[512] = {0};
			str_format(aChatmsg, sizeof(aChatmsg), Localize("You must wait %d seconds before making another vote", ClientID), (Timeleft/Server()->TickSpeed())+1);
			SendChatTarget(ClientID, aChatmsg);
			return;
		}

		char aChatmsg[512] = {0};
		char aDesc[VOTE_DESC_LENGTH] = {0};
		char aCmd[VOTE_CMD_LENGTH] = {0};
		CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
		const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";
		int KickID = -1;
		int SpectateID = -1;

		if(str_comp_nocase(pMsg->m_Type, "option") == 0)
		{
			CVoteOptionServer *pOption = m_pVoteOptionFirst;
			while(pOption)
			{
				if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
				{
					str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
					str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
					break;
				}

				pOption = pOption->m_pNext;
			}

			if(!pOption)
			{
				str_format(aChatmsg, sizeof(aChatmsg), Localize("'%s' isn't an option on this server", ClientID), pMsg->m_Value);
				SendChatTarget(ClientID, aChatmsg);
				return;
			}
		}
		else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
		{
			if(!g_Config.m_SvVoteKick)
			{
				SendChatTarget(ClientID, "Server does not allow voting to kick players");
				return;
			}

			if(g_Config.m_SvVoteKickMin)
			{
				int PlayerNum = 0;
				for(int i = 0; i < MAX_CLIENTS; ++i)
					if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
						++PlayerNum;

				if(PlayerNum < g_Config.m_SvVoteKickMin)
				{
					str_format(aChatmsg, sizeof(aChatmsg), Localize("Kick voting requires %d players on the server", ClientID), g_Config.m_SvVoteKickMin);
					SendChatTarget(ClientID, aChatmsg);
					return;
				}
			}

			KickID = str_toint(pMsg->m_Value);
			if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
			{
				SendChatTarget(ClientID, "Invalid client id to kick");
				return;
			}
			if(KickID == ClientID)
			{
				SendChatTarget(ClientID, "You can't kick yourself");
				return;
			}
			if(Server()->IsAuthed(KickID))
			{
				SendChatTarget(ClientID, "You can't kick admins");
				char aBufKick[128];
				str_format(aBufKick, sizeof(aBufKick), Localize("'%s' called for vote to kick you", KickID), Server()->ClientName(ClientID));
				SendChatTarget(KickID, aBufKick);
				return;
			}

			str_format(aDesc, sizeof(aDesc), "Kick '%s'", Server()->ClientName(KickID));
			if (!g_Config.m_SvVoteKickBantime)
				str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
			else
			{
				char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
				Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
				str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, g_Config.m_SvVoteKickBantime);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aCmd);
			}
		}
		else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
		{
			if(!g_Config.m_SvVoteSpectate)
			{
				SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
				return;
			}

			SpectateID = str_toint(pMsg->m_Value);
			if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
			{
				SendChatTarget(ClientID, "Invalid client id to move");
				return;
			}
			if(SpectateID == ClientID)
			{
				SendChatTarget(ClientID, "You can't move yourself");
				return;
			}

			str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
			str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		}

		if(aCmd[0])
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!m_apPlayers[i])
					continue;
				if(str_comp_nocase(pMsg->m_Type, "option") == 0)
					str_format(aChatmsg, sizeof(aChatmsg), Localize("'%s' called vote to change server option '%s' (%s)", i),
						Server()->ClientName(ClientID), aDesc, pReason);
				else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
					str_format(aChatmsg, sizeof(aChatmsg), Localize("'%s' called for vote to kick '%s' (%s)", i),
						Server()->ClientName(ClientID), Server()->ClientName(KickID), pReason);
				else
					str_format(aChatmsg, sizeof(aChatmsg), Localize("'%s' called for vote to move '%s' to spectators (%s)", i),
						Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
				CNetMsg_Sv_Chat Msg;
				Msg.m_Team = 0;
				Msg.m_ClientID = -1;
				Msg.m_pMessage = aChatmsg;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
			}
			StartVote(aDesc, aCmd, pReason);
			pPlayer->m_Vote = 1;
			pPlayer->m_VotePos = m_VotePos = 1;
			m_VoteCreator = ClientID;
			pPlayer->m_LastVoteCall = Now;
			if(m_SixupCallVoteForce)
			{
				m_SixupCallVoteForce = false;
				Console()->ExecuteLine(aCmd);
				m_VoteEnforce = VOTE_ENFORCE_YES;
				EndVote();
			}
		}
	}
	else if(MsgID == NETMSGTYPE_CL_VOTE)
	{
		if(!m_VoteCloseTime)
			return;

		if(pPlayer->m_Vote == 0)
		{
			CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
			if(!pMsg->m_Vote)
				return;

			pPlayer->m_Vote = pMsg->m_Vote;
			pPlayer->m_VotePos = ++m_VotePos;
			m_VoteUpdate = true;
		}
	}
	else if (MsgID == NETMSGTYPE_CL_SETTEAM && !m_World.m_Paused)
	{
		CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

		if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam+Server()->TickSpeed()*3 > Server()->Tick()))
			return;

		if(pPlayer->m_TeamChangeTick > Server()->Tick())
		{
			pPlayer->m_LastSetTeam = Server()->Tick();
			int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick())/Server()->TickSpeed();
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), Localize("Time to wait before changing team: %02d:%02d", ClientID), TimeLeft/60, TimeLeft%60);
			SendBroadcast(aBuf, ClientID);
			return;
		}

		// Switch team on given client and kill/respawn him
		if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
		{
			if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
					m_VoteUpdate = true;
				pPlayer->SetTeam(pMsg->m_Team);
				(void)m_pController->CheckTeamBalance();
				pPlayer->m_TeamChangeTick = Server()->Tick();
			}
			else
				SendBroadcast("Teams must be balanced, please join other team", ClientID);
		}
		else
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), Localize("Only %d active players are allowed", ClientID), g_Config.m_SvMaxClients-g_Config.m_SvSpectatorSlots);
			SendBroadcast(aBuf, ClientID);
		}
	}
	else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
	{
		CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

		if(pPlayer->GetTeam() != TEAM_SPECTATORS || pPlayer->m_SpectatorID == pMsg->m_SpectatorID || ClientID == pMsg->m_SpectatorID ||
			(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()*3 > Server()->Tick()))
			return;

		pPlayer->m_LastSetSpectatorMode = Server()->Tick();
		if(pMsg->m_SpectatorID != SPEC_FREEVIEW && (!m_apPlayers[pMsg->m_SpectatorID] || m_apPlayers[pMsg->m_SpectatorID]->GetTeam() == TEAM_SPECTATORS))
			SendChatTarget(ClientID, "Invalid spectator id used");
		else
			pPlayer->m_SpectatorID = pMsg->m_SpectatorID;
	}
	else if (MsgID == NETMSGTYPE_CL_STARTINFO)
	{
		if(pPlayer->m_IsReady)
			return;

		CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
		pPlayer->m_LastChangeInfo = Server()->Tick();

		// set start infos
		Server()->SetClientName(ClientID, pMsg->m_pName);
		Server()->SetClientClan(ClientID, pMsg->m_pClan);
		Server()->SetClientCountry(ClientID, pMsg->m_Country);
		str_copy(pPlayer->m_aLanguage, Localization()->GetLanguageCode(pMsg->m_Country), sizeof(pPlayer->m_aLanguage));
		if(!m_IpCountry.Empty())
		{
			char aAddr[NETADDR_MAXSTRSIZE] = {0};
			Server()->GetClientAddr(ClientID, aAddr, sizeof(aAddr));
			char aIso[8];
			if(aAddr[0] && m_IpCountry.Lookup(aAddr, aIso, sizeof(aIso)))
				str_copy(pPlayer->m_aLanguage, Localization()->GetLanguageCodeFromISO(aIso), sizeof(pPlayer->m_aLanguage));
		}
		str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
		pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
		pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
		pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
		m_pController->OnPlayerInfoChange(pPlayer);

		// send vote options
		CNetMsg_Sv_VoteClearOptions ClearMsg;
		Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

		CNetMsg_Sv_VoteOptionListAdd OptionMsg;
		int NumOptions = 0;
		OptionMsg.m_pDescription0 = "";
		OptionMsg.m_pDescription1 = "";
		OptionMsg.m_pDescription2 = "";
		OptionMsg.m_pDescription3 = "";
		OptionMsg.m_pDescription4 = "";
		OptionMsg.m_pDescription5 = "";
		OptionMsg.m_pDescription6 = "";
		OptionMsg.m_pDescription7 = "";
		OptionMsg.m_pDescription8 = "";
		OptionMsg.m_pDescription9 = "";
		OptionMsg.m_pDescription10 = "";
		OptionMsg.m_pDescription11 = "";
		OptionMsg.m_pDescription12 = "";
		OptionMsg.m_pDescription13 = "";
		OptionMsg.m_pDescription14 = "";
		CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
		while(pCurrent)
		{
			switch(NumOptions++)
			{
			case 0: OptionMsg.m_pDescription0 = pCurrent->m_aDescription; break;
			case 1: OptionMsg.m_pDescription1 = pCurrent->m_aDescription; break;
			case 2: OptionMsg.m_pDescription2 = pCurrent->m_aDescription; break;
			case 3: OptionMsg.m_pDescription3 = pCurrent->m_aDescription; break;
			case 4: OptionMsg.m_pDescription4 = pCurrent->m_aDescription; break;
			case 5: OptionMsg.m_pDescription5 = pCurrent->m_aDescription; break;
			case 6: OptionMsg.m_pDescription6 = pCurrent->m_aDescription; break;
			case 7: OptionMsg.m_pDescription7 = pCurrent->m_aDescription; break;
			case 8: OptionMsg.m_pDescription8 = pCurrent->m_aDescription; break;
			case 9: OptionMsg.m_pDescription9 = pCurrent->m_aDescription; break;
			case 10: OptionMsg.m_pDescription10 = pCurrent->m_aDescription; break;
			case 11: OptionMsg.m_pDescription11 = pCurrent->m_aDescription; break;
			case 12: OptionMsg.m_pDescription12 = pCurrent->m_aDescription; break;
			case 13: OptionMsg.m_pDescription13 = pCurrent->m_aDescription; break;
			case 14:
				{
					OptionMsg.m_pDescription14 = pCurrent->m_aDescription;
					OptionMsg.m_NumOptions = NumOptions;
					Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
					OptionMsg = CNetMsg_Sv_VoteOptionListAdd();
					NumOptions = 0;
					OptionMsg.m_pDescription1 = "";
					OptionMsg.m_pDescription2 = "";
					OptionMsg.m_pDescription3 = "";
					OptionMsg.m_pDescription4 = "";
					OptionMsg.m_pDescription5 = "";
					OptionMsg.m_pDescription6 = "";
					OptionMsg.m_pDescription7 = "";
					OptionMsg.m_pDescription8 = "";
					OptionMsg.m_pDescription9 = "";
					OptionMsg.m_pDescription10 = "";
					OptionMsg.m_pDescription11 = "";
					OptionMsg.m_pDescription12 = "";
					OptionMsg.m_pDescription13 = "";
					OptionMsg.m_pDescription14 = "";
				}
			}
			pCurrent = pCurrent->m_pNext;
		}
		if(NumOptions > 0)
		{
			OptionMsg.m_NumOptions = NumOptions;
			Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
			NumOptions = 0;
		}

		// send tuning parameters to client
		SendTuningParams(ClientID);

		// client is ready to enter
		pPlayer->m_IsReady = true;
		CNetMsg_Sv_ReadyToEnter m;
		Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
	}
	else if (MsgID == NETMSGTYPE_CL_CHANGEINFO)
	{
		if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*5 > Server()->Tick())
			return;

		CNetMsg_Cl_ChangeInfo *pMsg = (CNetMsg_Cl_ChangeInfo *)pRawMsg;
		pPlayer->m_LastChangeInfo = Server()->Tick();

		// set infos
		char aOldName[MAX_NAME_LENGTH];
		str_copy(aOldName, Server()->ClientName(ClientID), sizeof(aOldName));
		Server()->SetClientName(ClientID, pMsg->m_pName);
		if(str_comp(aOldName, Server()->ClientName(ClientID)) != 0)
		{
			char aChatText[256];
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!m_apPlayers[i])
					continue;
				str_format(aChatText, sizeof(aChatText), Localize("'%s' changed name to '%s'", i),
					aOldName, Server()->ClientName(ClientID));
				CNetMsg_Sv_Chat Msg;
				Msg.m_Team = 0;
				Msg.m_ClientID = -1;
				Msg.m_pMessage = aChatText;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
			}
		}
		Server()->SetClientClan(ClientID, pMsg->m_pClan);
		Server()->SetClientCountry(ClientID, pMsg->m_Country);
		str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
		pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
		pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
		pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
		m_pController->OnPlayerInfoChange(pPlayer);
		// refresh sixup clients (name/clan/skin)
		SendSixupClientDrop(ClientID, "", true);
		SendSixupClientInfos(ClientID);
	}
	else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
	{
		CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

		if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*3 > Server()->Tick())
			return;

		pPlayer->m_LastEmote = Server()->Tick();

		SendEmoticon(ClientID, pMsg->m_Emoticon);
		CCharacter *pCharacter = pPlayer->GetCharacter();
		if(pCharacter && pCharacter->IsAlive())
		{
			int Emote = EMOTE_NORMAL;
			switch(pMsg->m_Emoticon)
			{
			case 0:
			case 6:
			case 8: Emote = EMOTE_PAIN; break;
			case 1:
			case 7:
			case 13:
			case 15: Emote = EMOTE_SURPRISE; break;
			case 2:
			case 5:
			case 14: Emote = EMOTE_HAPPY; break;
			case 3:
			case 4:
			case 12: Emote = EMOTE_BLINK; break;
			case 9:
			case 10:
			case 11: Emote = EMOTE_ANGRY; break;
			}
			pCharacter->SetEmote(Emote, Server()->Tick()+Server()->TickSpeed()*2);
		}
	}
	else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
	{
		if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*3 > Server()->Tick())
			return;

		pPlayer->m_LastKill = Server()->Tick();
		pPlayer->KillCharacter(WEAPON_SELF);
	}
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(pResult->GetInteger(0));
	else
		pSelf->m_pController->StartRound();
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(pResult->GetString(0), -1);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = 0;
	if(pResult->NumArguments() > 2)
		Delay = pResult->GetInteger(2);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	if(!pSelf->m_apPlayers[ClientID])
		return;

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved all clients to team %d", Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->SetTeam(Team);

	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!pSelf->m_apPlayers[i])
						continue;
					str_format(aBuf, sizeof(aBuf), pSelf->Localize("admin forced server option '%s' (%s)", i), pValue, pReason);
					CNetMsg_Sv_Chat Msg;
					Msg.m_Team = 0;
					Msg.m_ClientID = -1;
					Msg.m_pMessage = aBuf;
					pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
				}
				pSelf->Console()->ExecuteLine(pOption->m_aCommand);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if (!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!pSelf->m_apPlayers[i])
				continue;
			str_format(aBuf, sizeof(aBuf), pSelf->Localize("admin moved '%s' to spectator (%s)", i),
				pSelf->Server()->ClientName(SpectateID), pReason);
			CNetMsg_Sv_Chat Msg;
			Msg.m_Team = 0;
			Msg.m_ClientID = -1;
			Msg.m_pMessage = aBuf;
			pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf);
	}
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pSelf->m_apPlayers[i])
			continue;
		str_format(aBuf, sizeof(aBuf), pSelf->Localize("admin forced vote %s", i), pResult->GetString(0));
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = -1;
		Msg.m_pMessage = aBuf;
		pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pLocalization = Kernel()->RequestInterface<ILocalization>();

	Console()->Register("tune", "si", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");

	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("restart", "?i", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds");
	Console()->Register("broadcast", "r", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "ss?r", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);
}

void CGameContext::OnInit(/*class IKernel *pKernel*/)
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pLocalization = Kernel()->RequestInterface<ILocalization>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);

	CIpCountryDb::SelfCheck();
	if(g_Config.m_SvIpinfoFile[0])
	{
		IStorage *pStorage = Kernel()->RequestInterface<IStorage>();
		if(pStorage)
			m_IpCountry.Load(pStorage, g_Config.m_SvIpinfoFile);
	}

	//if(!data) // only load once
		//data = load_data_from_memory(internal_data);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	// reset everything here
	//world = new GAMEWORLD;
	//players = new CPlayer[MAX_CLIENTS];

	// select gametype
	m_pController = new CGameControllerOpenBattle(this);

	// setup core world
	//for(int i = 0; i < MAX_CLIENTS; i++)
	//	game.players[i].core.world = &game.world.core;

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);




	/*
	num_spawn_points[0] = 0;
	num_spawn_points[1] = 0;
	num_spawn_points[2] = 0;
	*/

	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;
			vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);

			// Raw game-layer health station sits below ENTITY_OFFSET, so OnEntity
			// never sees it.
			if(Index == TILE_BF_HEALTH_STATION)
				new CSupplyStation(&m_World, Pos, 2);
			else if(Index >= ENTITY_OFFSET)
			{
				m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
			}
		}
	}

	//game.world.insert_entity(game.Controller);

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS-i-1);
		}
	}
#endif
}

void CGameContext::OnShutdown()
{
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

const char *CGameContext::GameType() { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
// This is wire-visible in the 0.6 server-browser reply.
const char *CGameContext::Version() { return "0.6 " MOD_NAME; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }

const char *CGameContext::Localize(const char *pText, int ClientID)
{
	if(!m_pLocalization || ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return pText;
	return m_pLocalization->Localize(m_apPlayers[ClientID]->m_aLanguage, pText);
}

void CGameContext::TrySendTip(int ClientID, int TipFlag, const char *pText)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID] || !pText)
		return;
	if(!m_apPlayers[ClientID]->TryConsumeTip(TipFlag))
		return;
	SendChatTarget(ClientID, pText);
}

void CGameContext::SendWelcomeTutorial(int ClientID)
{
	if(!g_Config.m_SvTutorial || !m_apPlayers[ClientID])
		return;
	if(!m_apPlayers[ClientID]->TryConsumeTip(CPlayer::TIP_WELCOME))
		return;

	SendChatTarget(ClientID, "Welcome to OpenBattle — capture the flag and hold checkpoints A/B/C.");
	SendChatTarget(ClientID, "Type /help to learn classes, vehicles, and objectives.");
	SendChatTarget(ClientID, "Step on a class tile (Soldier/Engineer/Medic/Sniper) before fighting.");
	SendChatTarget(ClientID, "Type /cmds for the full command list.");
}

bool CGameContext::HandleChatCommand(int ClientID, const char *pMessage)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if(!pPlayer || !pMessage || pMessage[0] != '/')
		return false;

	SendChatTarget(ClientID, "==== ## -- ## ====");

	if(str_comp_nocase(pMessage, "/e") == 0)
	{
		CCharacter *pCharacter = pPlayer->GetCharacter();
		if(pCharacter)
			pCharacter->HandleBattlefieldExitCommand();
		return true;
	}

	if(str_comp_nocase_num(pMessage, "/language ", 10) == 0 ||
		str_comp_nocase_num(pMessage, "/lang ", 6) == 0)
	{
		const char *pLang = str_comp_nocase_num(pMessage, "/language ", 10) == 0 ?
			pMessage + 10 : pMessage + 6;
		while(*pLang == ' ')
			pLang++;
		if(!pLang[0])
		{
			SendChatTarget(ClientID, "Usage: /language zh-cn");
			return true;
		}
		str_copy(pPlayer->m_aLanguage, pLang, sizeof(pPlayer->m_aLanguage));
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Localize("Language set to '%s'", ClientID), pPlayer->m_aLanguage);
		SendChatTarget(ClientID, aBuf);
		return true;
	}
	if(str_comp_nocase(pMessage, "/language") == 0 ||
		str_comp_nocase(pMessage, "/lang") == 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), Localize("Current language: %s", ClientID), pPlayer->m_aLanguage);
		SendChatTarget(ClientID, aBuf);
		SendChatTarget(ClientID, "Usage: /language zh-cn");
		return true;
	}

	if(str_comp_nocase(pMessage, "/info") == 0)
	{
		SendChatTarget(ClientID, MOD_NAME " - open-source version by " MOD_AUTHOR);
		SendChatTarget(ClientID, "Original mod by " MOD_ORIGINAL_AUTHOR ".");
		SendChatTarget(ClientID, "Type /help for gameplay help.");
		return true;
	}

	if(str_comp_nocase(pMessage, "/cmds") == 0)
	{
		SendChatTarget(ClientID, "/help [class|controls|vehicles|objectives]");
		SendChatTarget(ClientID, "/info  /cmds  /objective  /e  /language <code>");
		return true;
	}

	if(str_comp_nocase(pMessage, "/objective") == 0)
	{
		m_pController->SendObjectiveStatus(ClientID);
		return true;
	}

	if(str_comp_nocase_num(pMessage, "/help", 5) == 0 &&
		(pMessage[5] == 0 || pMessage[5] == ' '))
	{
		const char *pTopic = pMessage + 5;
		while(*pTopic == ' ')
			pTopic++;

		if(!pTopic[0])
		{
			SendChatTarget(ClientID, "OpenBattle: team CTF + checkpoints. Pick a class tile first.");
			SendChatTarget(ClientID, "/help class | controls | vehicles | objectives");
			SendChatTarget(ClientID, "Shift or /e exits vehicles. Emote or /e throws a hand grenade (Soldier).");
			return true;
		}
		if(str_comp_nocase(pTopic, "class") == 0)
		{
			SendChatTarget(ClientID, "Classes (step on map tiles):");
			SendChatTarget(ClientID, "Soldier: C4 + ammo packs + hand grenade. Engineer: mines, repair, hack doors.");
			SendChatTarget(ClientID, "Medic: heal shots. Sniper: rifle + stealth (Hammer toggles invis).");
			return true;
		}
		if(str_comp_nocase(pTopic, "controls") == 0)
		{
			SendChatTarget(ClientID, "Shift or /e: leave vehicle / cannon / fire smoke.");
			SendChatTarget(ClientID, "Emote or /e (Soldier): throw hand grenade. Space: fire water weapon.");
			SendChatTarget(ClientID, "Anti-Tank: mouse wheel switches aim / manual mode.");
			return true;
		}
		if(str_comp_nocase(pTopic, "vehicles") == 0)
		{
			SendChatTarget(ClientID, "Walk into a team vehicle to board. Heli/Tank can carry a passenger.");
			SendChatTarget(ClientID, "Enemy vehicles need the matching key — even empty ones cannot be seized. Engineers can only damage occupied enemy vehicles. Exit: Shift or /e.");
			return true;
		}
		if(str_comp_nocase(pTopic, "objectives") == 0)
		{
			SendChatTarget(ClientID, "A starts red, B starts blue, and neutral C unlocks after 20 seconds.");
			SendChatTarget(ClientID, "Checkpoint capture accelerates with teammates, freezes when contested, and rolls back after 3 empty seconds.");
			SendChatTarget(ClientID, "Dynamic objectives rotate between flags, C, enemy checkpoints, breakthroughs, area control, and combined arms.");
			SendChatTarget(ClientID, "Use /objective for the current objective, team stage, timer, and comeback bonus.");
			return true;
		}
		SendChatTarget(ClientID, "Unknown help topic. Try: class, controls, vehicles, objectives.");
		return true;
	}

	return false;
}

IGameServer *CreateGameServer() { return new CGameContext; }
