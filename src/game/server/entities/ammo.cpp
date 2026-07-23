#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/sixup_snap.h>

#include "ammo.h"
#include "character.h"

CAmmo::CAmmo(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_CurrentPos = Pos;
	m_ProximityRadius = 25.0f;
	m_Owner = Owner;
	m_Vel = vec2(0, 0);
	m_AnimTimer = 0;
	m_MessageCooldown = 0;
	AllocExtraIDs(1);
	GameWorld()->InsertEntity(this);
}

void CAmmo::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CAmmo::Snap(int SnappingClient)
{
	bool Sixup = SnappingClient >= 0 && Server()->IsSixup(SnappingClient);
	CNetObj_Pickup *pPickup = static_cast<CNetObj_Pickup *>(
		Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, PickupSnapSize(Sixup)));
	if(!pPickup)
		return;

	pPickup->m_X = round_to_int(m_CurrentPos.x);
	pPickup->m_Y = round_to_int(m_CurrentPos.y);
	int Subtype = 0;
	if(m_AnimTimer > Server()->TickSpeed()*2)
		Subtype = 4;
	else if(m_AnimTimer > Server()->TickSpeed()*1.5f)
		Subtype = 2;
	else if(m_AnimTimer > Server()->TickSpeed())
		Subtype = 3;
	else if(m_AnimTimer > Server()->TickSpeed()*0.5f)
		Subtype = 1;
	else
	{
		Subtype = 0;
		if(m_AnimTimer == 0)
			m_AnimTimer = round_to_int(Server()->TickSpeed()*2.5f);
	}
	if(Sixup)
		pPickup->m_Type = PickupTypeSeven(POWERUP_WEAPON, Subtype);
	else
	{
		pPickup->m_Type = POWERUP_WEAPON;
		pPickup->m_Subtype = Subtype;
	}
}

void CAmmo::Hit()
{
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_CurrentPos, 30.0f, 0);
	if(!pTarget || !pTarget->IsAlive())
		return;
	// These counters are replenished before CAmmo's ten-second self-use gate.
	pTarget->RefillBattlefieldWaterAmmo();
	if(pTarget->AmmoSupplyCoolingDown())
	{
		if(m_MessageCooldown == 0)
		{
			GameServer()->SendChatTarget(pTarget->GetPlayer()->GetCID(),
				"You can only give all 10 seconds ammo for yourself!");
			m_MessageCooldown = Server()->TickSpeed()*3;
		}
		return;
	}

	bool Refilled = pTarget->RefillBattlefieldAmmo();
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(pOwner)
	{
		pOwner->ReleaseAmmoPackSlot();
		if(pOwner != pTarget)
			pOwner->GetPlayer()->m_Score++;
		else
			pOwner->StartAmmoSupplyCooldown();
	}
	if(Refilled)
		GameServer()->CreateSound(m_CurrentPos, SOUND_PICKUP_GRENADE);
	GameWorld()->DestroyEntity(this);
}

void CAmmo::Tick()
{
	if(m_MessageCooldown > 0)
		m_MessageCooldown--;
	if(m_AnimTimer > 0)
		m_AnimTimer--;
	if(!GameServer()->GetPlayerChar(m_Owner))
		GameWorld()->DestroyEntity(this);

	if(GameServer()->Collision()->isWater(round_to_int(m_CurrentPos.x), round_to_int(m_CurrentPos.y)))
		m_Vel.y = 3.0f;
	else
		m_Vel.y += GameServer()->Tuning()->m_Gravity;
	if(m_Vel.x > -0.1f && m_Vel.x < 0.1f)
		m_Vel.x = 0.0f;
	GameServer()->Collision()->MoveBox(
		&m_CurrentPos, &m_Vel, vec2(25, 25), 0.5f);
	Hit();
}
