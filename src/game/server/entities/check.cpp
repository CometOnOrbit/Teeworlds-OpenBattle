#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "check.h"

CCheck::CCheck(CGameWorld *pGameWorld, vec2 From, vec2 To, int Type, int Team)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = From;
	m_To = To;
	m_Type = Type;
	m_Team = Team;
	AllocExtraIDs(1);
	GameWorld()->InsertEntity(this);
}

void CCheck::Reset()
{
}

void CCheck::DoBounce()
{
}

void CCheck::SnapLaser(int ID, vec2 From, vec2 To)
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

void CCheck::Snap(int SnappingClient)
{
	(void)SnappingClient;
	if(m_Type >= 1 && m_Type <= 3)
	{
		int State = GameServer()->m_aCheckpointState[m_Type-1];
		if((m_Team == 1 && State < 0) || (m_Team == 2 && State > 0))
		{
			SnapLaser(m_ID, m_Pos, m_To);
			SnapLaser(m_aExtraIDs[0], m_To, m_To);
		}
		return;
	}

	if(m_Type == 4 && m_Team >= 1 && m_Team <= 3)
	{
		int State = GameServer()->m_aCheckpointState[m_Team-1];
		CNetObj_Flag *pFlag = static_cast<CNetObj_Flag *>(
			Server()->SnapNewItem(NETOBJTYPE_FLAG, m_aExtraIDs[0], sizeof(CNetObj_Flag)));
		if(!pFlag)
			return;
		pFlag->m_X = round_to_int(m_Pos.x)+2;
		if(State < 1)
		{
			pFlag->m_Y = round_to_int(m_Pos.y)+State/2;
			pFlag->m_Team = TEAM_RED;
		}
		else
		{
			pFlag->m_Y = round_to_int(m_Pos.y)-State/2;
			pFlag->m_Team = TEAM_BLUE;
		}
	}
}

void CCheck::HitCharacter()
{
	vec2 Intersection;
	CCharacter *pCharacter = GameWorld()->IntersectCharacter(
		m_Pos, m_To, 0.0f, Intersection, 0);
	if(!pCharacter || !pCharacter->IsAlive() || m_Type < 1 || m_Type > 3)
		return;

	int State = GameServer()->m_aCheckpointState[m_Type-1];
	int Team = pCharacter->GetPlayer()->GetTeam();
	CPlayer *pPlayer = pCharacter->GetPlayer();
	if(!pPlayer->IsSoldier() && !pPlayer->IsEngineer() &&
		!pPlayer->IsMedic() && !pPlayer->IsSniper())
		return;
	if((Team == TEAM_RED && State < 0 && m_Team == 1) ||
		(Team == TEAM_BLUE && State > 0 && m_Team == 2))
	{
		pCharacter->Tele(GameServer()->m_aCheckpointDestination[m_Type-1]);
	}
}

void CCheck::Tick()
{
	if(m_Team == 1 || m_Team == 2)
		HitCharacter();

	if(m_Type == 4 && m_Team >= 1 && m_Team <= 3)
	{
		CCharacter *pCharacter = GameWorld()->ClosestCharacter(m_Pos, 10.0f, 0);
		if(pCharacter && pCharacter->IsAlive())
			pCharacter->Check(m_Team);
	}
}
