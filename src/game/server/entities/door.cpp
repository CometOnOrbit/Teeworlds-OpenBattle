#include <list>

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include "character.h"
#include "door.h"

CDoor::CDoor(CGameWorld *pGameWorld, vec2 From, vec2 To, int Number)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = From;
	m_To = To;
	m_Number = Number;
	AllocExtraIDs(1);
	GameWorld()->InsertEntity(this);
}

void CDoor::Reset()
{
}

void CDoor::DoBounce()
{
}

void CDoor::SnapLaser(int ID, vec2 From, vec2 To)
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

void CDoor::Snap(int SnappingClient)
{
	(void)SnappingClient;
	if(m_Number < 1 || m_Number > 6)
		return;

	if(GameServer()->m_aDoorState[m_Number-1] == 1)
		SnapLaser(m_ID, m_Pos, m_To);
	else if(GameServer()->m_aDoorState[m_Number-1] == 0)
		SnapLaser(m_ID, m_Pos, m_Pos);

	SnapLaser(m_aExtraIDs[0], m_To, m_To);
}

bool CDoor::HitCharacter()
{
	if(m_Number < 1 || m_Number > 6 ||
		GameServer()->m_aDoorState[m_Number-1] != 1)
		return true;

	std::list<CCharacter *> Characters = GameWorld()->IntersectedCharacters(
		m_Pos, m_To, 0.0f, 0);
	for(std::list<CCharacter *>::iterator It = Characters.begin();
		It != Characters.end(); ++It)
	{
		CCharacter *pCharacter = *It;
		if(pCharacter && pCharacter->IsAlive())
		{
			// Abort the entire door hit pass when the line query
			// catches a character more than 20 pixels below the lower endpoint
			// of a vertical door.
			if(m_Pos.x == m_To.x &&
				pCharacter->m_Pos.y < min(m_Pos.y, m_To.y)-20.0f)
				return false;
			pCharacter->CollideWithDoor(m_Pos, m_To);
		}
	}
	return !Characters.empty();
}

void CDoor::Tick()
{
	if(m_Number >= 1 && m_Number <= 6)
		m_To = GameServer()->m_aDoorEnd[m_Number-1];
	HitCharacter();
}
