#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include "ck_point_flag.h"

CCKPointFlag::CCKPointFlag(CGameWorld *pGameWorld, vec2 Pos, int Number)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Number = Number;
	GameWorld()->InsertEntity(this);
}

void CCKPointFlag::Reset()
{
}

void CCKPointFlag::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;
	int Team = GameServer()->m_pController->PointFlagTeam(m_Number);
	if(Team < TEAM_RED || Team > TEAM_BLUE)
		return;
	CNetObj_Flag *pFlag = static_cast<CNetObj_Flag *>(Server()->SnapNewItem(NETOBJTYPE_FLAG, m_ID, sizeof(CNetObj_Flag)));
	if(!pFlag)
		return;
	pFlag->m_X = round_to_int(m_Pos.x);
	pFlag->m_Y = round_to_int(m_Pos.y);
	pFlag->m_Team = Team;
}
