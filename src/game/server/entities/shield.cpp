#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/sixup_snap.h>

#include <math.h>

#include "character.h"
#include "shield.h"

static vec2 ShieldOrbitOffset(int Tick, int OrbitSlot, float Radius)
{
	// CShield::Tick feeds tick * 0.1 - {0, 90, 180} directly into
	// cosf/sinf.  This is deliberately radians, unlike GetDir's degree API.
	float PhaseOffset = OrbitSlot == 1 ? 0.0f : OrbitSlot == 2 ? 90.0f : 180.0f;
	float Phase = Tick*0.1f-PhaseOffset;
	return vec2(cosf(Phase)*Radius, sinf(Phase)*Radius);
}

CShield::CShield(CGameWorld *pGameWorld, vec2 Pos, int Owner, int Damage, bool Explosive,
	int OrbitSlot)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_ProximityRadius = 14.0f;
	m_Pos = Pos;
	m_Owner = Owner;
	m_Damage = Damage;
	m_Explosive = Explosive;
	m_OrbitSlot = OrbitSlot;
	m_LifeTimer = Server()->TickSpeed()*5;
	m_Vel = vec2(0, 0);
	GameWorld()->InsertEntity(this);
}

void CShield::Tick()
{
	if(m_LifeTimer > 0)
		m_LifeTimer--;
	if(m_LifeTimer == 0)
		GameWorld()->DestroyEntity(this);

	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	// Select the contact before advancing the orbit for this tick.
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_Pos, 20.0f, 0);
	if(GameWorld()->m_Paused)
		GameWorld()->DestroyEntity(this);
	if(!pOwner || !pOwner->IsAlive())
	{
		m_Pos += ShieldOrbitOffset(Server()->Tick(), m_OrbitSlot, -60.0f);
		m_Owner = -1;
		GameWorld()->DestroyEntity(this);
		return;
	}

	m_Vel = pOwner->GetVelocity()*0.2f;
	m_Pos = pOwner->m_Pos+ShieldOrbitOffset(Server()->Tick(), m_OrbitSlot, 60.0f);

	if(pTarget && pTarget->IsAlive() && pTarget->GetPlayer() &&
		pTarget->GetPlayer()->GetCID() != m_Owner)
	{
		pTarget->TakeDamage(vec2(0, 0), m_Damage, m_Owner, WEAPON_NINJA);
		GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_NINJA, false);
	}
}

void CShield::Snap(int SnappingClient)
{
	bool Sixup = SnappingClient >= 0 && Server()->IsSixup(SnappingClient);
	CNetObj_Pickup *pPickup = static_cast<CNetObj_Pickup *>(
		Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, PickupSnapSize(Sixup)));
	if(!pPickup)
		return;
	pPickup->m_X = round_to_int(m_Pos.x);
	pPickup->m_Y = round_to_int(m_Pos.y);
	pPickup->m_Type = POWERUP_HEALTH;
	if(!Sixup)
		pPickup->m_Subtype = 0;
}
