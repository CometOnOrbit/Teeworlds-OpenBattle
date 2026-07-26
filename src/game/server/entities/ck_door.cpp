#include <list>

#include <base/math.h>
#include <game/generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>

#include "ck_door.h"

CCKDoor::CCKDoor(CGameWorld *pGameWorld, vec2 From, vec2 To, int Number)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = From;
	m_To = To;
	m_Number = Number;
	GameWorld()->InsertEntity(this);
}

void CCKDoor::Reset()
{
}

void CCKDoor::SnapLaser(int ID, vec2 From, vec2 To)
{
	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, ID, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;
	pLaser->m_X = round_to_int(From.x);
	pLaser->m_Y = round_to_int(From.y);
	pLaser->m_FromX = round_to_int(To.x);
	pLaser->m_FromY = round_to_int(To.y);
	pLaser->m_StartTick = Server()->Tick();
}

void CCKDoor::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient, m_Pos) && NetworkClipped(SnappingClient, m_To))
		return;
	if(GameServer()->m_pController->IsDoorClosed(m_Number))
		SnapLaser(m_ID, m_Pos, m_To);
}

bool CCKDoor::Intersects(vec2 From, vec2 To, vec2 *pHit, float Radius) const
{
	float PathLength = distance(From, To);
	int Steps = max(1, (int)ceilf(PathLength));
	for(int Step = 0; Step <= Steps; ++Step)
	{
		vec2 Point = From+(To-From)*(Step/(float)Steps);
		vec2 Closest = distance(m_Pos, m_To) > 0.001f ? closest_point_on_line(m_Pos, m_To, Point) : m_Pos;
		if(distance(Point, Closest) < Radius)
		{
			if(pHit)
				*pHit = Point;
			return true;
		}
	}
	return false;
}

void CCKDoor::Tick()
{
	if(!GameServer()->m_pController->IsDoorClosed(m_Number))
		return;
	std::list<CCharacter *> Characters = GameWorld()->IntersectedCharacters(m_Pos, m_To, 0.0f, 0, true);
	for(std::list<CCharacter *>::iterator It = Characters.begin(); It != Characters.end(); ++It)
	{
		CCharacter *pCharacter = *It;
		if(!pCharacter || !pCharacter->IsAlive() || !pCharacter->GetPlayer())
			continue;
		bool Blocks = GameServer()->m_pController->DoorBlocksCharacter(m_Number, pCharacter->GetPlayer()->GetTeam());
		if(Blocks)
			pCharacter->CollideWithCKDoor(m_Pos, m_To);
	}
}
