#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include "ck_teleport_marker.h"

CCKTeleportMarker::CCKTeleportMarker(CGameWorld *pGameWorld, vec2 From, vec2 To, int Number, int Side)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = From;
	m_To = To;
	m_Number = Number;
	m_Side = Side;
	GameWorld()->InsertEntity(this);
}

void CCKTeleportMarker::Reset()
{
}

void CCKTeleportMarker::Snap(int SnappingClient)
{
	if((NetworkClipped(SnappingClient, m_Pos) && NetworkClipped(SnappingClient, m_To)) || !GameServer()->m_pController->TeleportEnabled(m_Number, m_Side))
		return;

	if(GameServer()->m_pController->IntersectDoor(m_Pos, m_To, 0, 18.0f))
		return;

	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;
	pLaser->m_X = round_to_int(m_Pos.x);
	pLaser->m_Y = round_to_int(m_Pos.y);
	pLaser->m_FromX = round_to_int(m_To.x);
	pLaser->m_FromY = round_to_int(m_To.y);
	pLaser->m_StartTick = Server()->Tick();
}
