#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/sixup_snap.h>

#include "supply_station.h"
#include "character.h"
#include "health_shot.h"

CSupplyStation::CSupplyStation(CGameWorld *pGameWorld, vec2 Pos, int Type)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	AllocExtraIDs(4);
	m_CurrentPos = Pos;
	m_NumHealthShots = 0;
	m_ProximityRadius = 14.0f;
	m_Type = Type;
	m_Vel = vec2(0, 0);
	m_AmmoCooldown = 0;
	m_AnimTimer = 0;
	m_HealthDowntime = 0;
	m_FirstHealthCooldown = 0;
	m_SecondHealthCooldown = 0;
	GameWorld()->InsertEntity(this);
}

void CSupplyStation::Reset()
{
}

void CSupplyStation::SnapPickup(int Type, int Subtype)
{
	bool Sixup = m_SnapClient >= 0 && Server()->IsSixup(m_SnapClient);
	CNetObj_Pickup *pPickup = static_cast<CNetObj_Pickup *>(
		Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_aExtraIDs[3], PickupSnapSize(Sixup)));
	if(!pPickup)
		return;
	pPickup->m_X = round_to_int(m_CurrentPos.x);
	pPickup->m_Y = round_to_int(m_CurrentPos.y);
	if(Sixup)
		pPickup->m_Type = PickupTypeSeven(Type, Subtype);
	else
	{
		pPickup->m_Type = Type;
		pPickup->m_Subtype = Subtype;
	}
}

void CSupplyStation::SnapLaser(int ID, vec2 From, vec2 To)
{
	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(
		Server()->SnapNewItem(NETOBJTYPE_LASER, ID, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;
	pLaser->m_X = round_to_int(From.x);
	pLaser->m_Y = round_to_int(From.y);
	pLaser->m_FromX = round_to_int(To.x);
	pLaser->m_FromY = round_to_int(To.y);
	pLaser->m_StartTick = Server()->Tick()-3;
}

void CSupplyStation::Snap(int SnappingClient)
{
	m_SnapClient = SnappingClient;
	if(m_Type == 1 && m_AmmoCooldown == 0)
	{
		int Subtype = 0;
		if(m_AnimTimer > Server()->TickSpeed()*2)
			Subtype = 4;
		else if(m_AnimTimer > Server()->TickSpeed()*1.5f)
			Subtype = 2;
		else if(m_AnimTimer > Server()->TickSpeed())
			Subtype = 3;
		else if(m_AnimTimer > Server()->TickSpeed()*0.5f)
			Subtype = 1;
		else if(m_AnimTimer == 0)
			m_AnimTimer = round_to_int(Server()->TickSpeed()*2.5f);
		SnapPickup(POWERUP_WEAPON, Subtype);
	}
	else if(m_Type == 2 && m_HealthDowntime == 0)
	{
		SnapPickup(POWERUP_HEALTH, 0);
	}
	else if(m_Type == 3 && m_AmmoCooldown == 0)
	{
		SnapPickup(POWERUP_WEAPON, 0);
	}

	SnapLaser(m_aExtraIDs[0], m_CurrentPos+vec2(-15, 15), m_CurrentPos+vec2(15, 15));
	SnapLaser(m_aExtraIDs[1], m_CurrentPos+vec2(15, 15), m_CurrentPos+vec2(45, 45));
	SnapLaser(m_aExtraIDs[2], m_CurrentPos+vec2(-15, 15), m_CurrentPos+vec2(-45, 45));
}

void CSupplyStation::Hit()
{
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_CurrentPos, 30.0f, 0);
	if(!pTarget || !pTarget->IsAlive() || m_AmmoCooldown)
		return;

	if(pTarget->RefillBattlefieldAmmo(true))
	{
		m_AmmoCooldown = Server()->TickSpeed()*5;
		GameServer()->CreateSound(m_CurrentPos, SOUND_PICKUP_GRENADE);
	}
}

void CSupplyStation::FireHealthShots()
{
	if(m_HealthDowntime)
		return;

	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_CurrentPos, 500.0f, 0);
	if(!pTarget || !pTarget->IsAlive() || m_FirstHealthCooldown ||
		pTarget->GetHealth() >= 10 ||
		GameServer()->Collision()->IntersectLine(m_CurrentPos, pTarget->m_Pos, 0, 0))
		return;

	new CHealthShot(GameWorld(), m_CurrentPos,
		normalize(pTarget->m_Pos-m_CurrentPos), 0, 2);
	m_FirstHealthCooldown = round_to_int(Server()->TickSpeed()*0.5f);
	m_NumHealthShots++;

	CCharacter *pSecond = GameWorld()->ClosestCharacter(m_CurrentPos, 500.0f, pTarget);
	if(!pSecond || !pSecond->IsAlive() || m_SecondHealthCooldown ||
		pSecond->GetHealth() >= 10 ||
		GameServer()->Collision()->IntersectLine(m_CurrentPos, pSecond->m_Pos, 0, 0))
		return;

	new CHealthShot(GameWorld(), m_CurrentPos,
		normalize(pSecond->m_Pos-m_CurrentPos), 0, 2);
	m_SecondHealthCooldown = round_to_int(Server()->TickSpeed()*0.5f);
	m_NumHealthShots++;
}

void CSupplyStation::Tick()
{
	m_Vel.y = 0.5f;
	GameServer()->Collision()->MoveBox(
		&m_CurrentPos, &m_Vel, vec2(1, 60), 0.5f);

	if(m_Type == 1)
	{
		if(m_AmmoCooldown > 0)
			m_AmmoCooldown--;
		if(m_AnimTimer > 0)
			m_AnimTimer--;
		if(m_AmmoCooldown == 1)
			GameServer()->CreateSound(m_CurrentPos, SOUND_WEAPON_SPAWN);
		Hit();
	}
	else if(m_Type == 2)
	{
		FireHealthShots();
		if(m_FirstHealthCooldown > 0)
			m_FirstHealthCooldown--;
		if(m_SecondHealthCooldown > 0)
			m_SecondHealthCooldown--;
		if(m_HealthDowntime > 0)
			m_HealthDowntime--;
		if(m_NumHealthShots == 10)
		{
			m_NumHealthShots = 0;
			m_HealthDowntime = Server()->TickSpeed()*5;
		}
		if(m_HealthDowntime == 1)
			GameServer()->CreateSound(m_CurrentPos, SOUND_WEAPON_SPAWN);
	}
}
