#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "switch.h"

CSwitch::CSwitch(CGameWorld *pGameWorld, vec2 Pos, int Number)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Number = Number;
	m_Cooldown = 0;
	m_Hacking = false;
	m_HackerPos = vec2(0, 0);
	m_HackProgress = 0;
	AllocExtraIDs(5);
	GameWorld()->InsertEntity(this);
}

void CSwitch::Reset()
{
}

void CSwitch::SnapLaser(int ID, vec2 From, vec2 To)
{
	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(
		Server()->SnapNewItem(NETOBJTYPE_LASER, ID, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;
	pLaser->m_X = round_to_int(From.x);
	pLaser->m_Y = round_to_int(From.y);
	pLaser->m_FromX = round_to_int(To.x);
	pLaser->m_FromY = round_to_int(To.y);
	pLaser->m_StartTick = Server()->Tick();
}

void CSwitch::Snap(int SnappingClient)
{
	(void)SnappingClient;
	if(m_Number < 1 || m_Number > 6)
		return;

	if(GameServer()->m_aDoorState[m_Number-1] == 1)
	{
		SnapLaser(m_ID, m_Pos, m_Pos);
		SnapLaser(m_aExtraIDs[1], m_Pos+vec2(0, -15), m_Pos+vec2(15, 0));
		SnapLaser(m_aExtraIDs[2], m_Pos+vec2(15, 0), m_Pos+vec2(0, 15));
		SnapLaser(m_aExtraIDs[3], m_Pos+vec2(0, 15), m_Pos+vec2(-15, 0));
		SnapLaser(m_aExtraIDs[4], m_Pos+vec2(-15, 0), m_Pos+vec2(0, -15));
	}
	else
	{
		SnapLaser(m_ID, m_Pos, m_Pos);
	}

	if(m_Hacking)
		SnapLaser(m_aExtraIDs[0], m_Pos, m_HackerPos);
}

void CSwitch::Toggle()
{
	if(m_Number < 1 || m_Number > 6)
		return;
	int &State = GameServer()->m_aDoorState[m_Number-1];
	if(State == 1)
		State = 0;
	else if(State == 0)
		State = 1;
}

bool CSwitch::HitCharacter()
{
	CCharacter *pHacker = GameWorld()->ClosestCharacter(m_Pos, 200.0f, 0);
	if(pHacker && pHacker->IsAlive() &&
		!GameServer()->Collision()->IntersectLine(m_Pos, pHacker->m_Pos, 0, 0))
	{
		int Team = pHacker->GetPlayer()->GetTeam();
		bool EnemyDoor = (m_Number == 5 && Team == TEAM_BLUE) ||
			(m_Number == 6 && Team == TEAM_RED);
		if(EnemyDoor && pHacker->GetPlayer()->IsEngineer() && pHacker->IsEngineerAction())
		{
			m_Hacking = true;
			m_HackerPos = pHacker->m_Pos;
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Hacking Enemy-Door: %i | 125", pHacker->GetPlayer()->GetCID()), m_HackProgress);
			GameServer()->SendBroadcast(aBuf, pHacker->GetPlayer()->GetCID());
			pHacker->SetBattlefieldBroadcastTimer(20);
			if(m_HackProgress >= 125)
			{
				Toggle();
				m_HackProgress = 0;
				GameServer()->CreatePlayerSpawn(m_Pos);
			}
		}
	}

	if(m_Cooldown)
		return true;

	CCharacter *pCharacter = GameWorld()->ClosestCharacter(m_Pos, 20.0f, 0);
	if(!pCharacter || !pCharacter->IsAlive())
		return true;

	int Team = pCharacter->GetPlayer()->GetTeam();
	if((m_Number == 5 && Team == TEAM_BLUE) ||
		(m_Number == 6 && Team == TEAM_RED))
	{
		GameServer()->SendChatTarget(pCharacter->GetPlayer()->GetCID(),
			"This is the door of the enemys, you can't use it!");
		GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH);
		m_Cooldown = Server()->TickSpeed();
		return true;
	}

	Toggle();
	m_Cooldown = Server()->TickSpeed();
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
	return true;
}

void CSwitch::Tick()
{
	if(!m_Hacking)
	{
		if(m_HackProgress > 0)
			m_HackProgress--;
	}
	else
	{
		m_HackProgress++;
	}

	if(m_Cooldown > 0)
		m_Cooldown--;
	m_Hacking = false;
	HitCharacter();
}
