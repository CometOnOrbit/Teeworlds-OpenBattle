#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include "character.h"
#include "health.h"
#include "health_shot.h"

CHealth::CHealth(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_ProximityRadius = 25.0f;
	m_CurrentPos = Pos;
	m_Owner = Owner;
	m_NumShots = 0;
	m_Vel = vec2(0, 0);
	m_FirstCooldown = 0;
	m_SecondCooldown = 0;
	GameWorld()->InsertEntity(this);
}

void CHealth::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CHealth::Snap(int SnappingClient)
{
	(void)SnappingClient;
	CNetObj_Pickup *pPickup = static_cast<CNetObj_Pickup *>(
		Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, sizeof(CNetObj_Pickup)));
	if(!pPickup)
		return;
	pPickup->m_X = round_to_int(m_CurrentPos.x);
	pPickup->m_Y = round_to_int(m_CurrentPos.y);
	pPickup->m_Type = POWERUP_HEALTH;
	pPickup->m_Subtype = 0;
}

void CHealth::Hit()
{
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_CurrentPos, 500.0f, 0);
	if(!pTarget || !pTarget->IsAlive() || m_FirstCooldown ||
		pTarget->GetHealth() >= 10 ||
		GameServer()->Collision()->IntersectLine(m_CurrentPos, pTarget->m_Pos, 0, 0))
		return;

	new CHealthShot(GameWorld(), m_CurrentPos,
		normalize(pTarget->m_Pos-m_CurrentPos), m_Owner, 2);
	m_FirstCooldown = round_to_int(Server()->TickSpeed()*0.5f);
	m_NumShots++;

	CCharacter *pSecond = GameWorld()->ClosestCharacter(m_CurrentPos, 500.0f, pTarget);
	if(!pSecond || !pSecond->IsAlive() || m_SecondCooldown ||
		pSecond->GetHealth() >= 10 ||
		GameServer()->Collision()->IntersectLine(m_CurrentPos, pSecond->m_Pos, 0, 0))
		return;

	new CHealthShot(GameWorld(), m_CurrentPos,
		normalize(pSecond->m_Pos-m_CurrentPos), m_Owner, 2);
	m_SecondCooldown = round_to_int(Server()->TickSpeed()*0.5f);
	m_NumShots++;
}

void CHealth::Tick()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
		GameWorld()->DestroyEntity(this);

	m_Vel.y += GameServer()->Tuning()->m_Gravity;
	if(m_Vel.x <= -0.1f || m_Vel.x >= 0.1f)
		m_Vel.x += m_Vel.x < 0.0f ? 0.055f : -0.055f;
	else
		m_Vel.x = 0.0f;

	GameServer()->Collision()->MoveBox(
		&m_CurrentPos, &m_Vel, vec2(25, 25), 0.5f);
	Hit();

	if(m_FirstCooldown > 0)
		m_FirstCooldown--;
	if(m_SecondCooldown > 0)
		m_SecondCooldown--;

	if(m_NumShots == 10)
	{
		GameWorld()->DestroyEntity(this);
		if(pOwner)
			pOwner->ReleaseHealthKitSlot();
	}
}
