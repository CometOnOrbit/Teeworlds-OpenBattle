#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>

#include "bomb.h"
#include "character.h"

CBomb::CBomb(CGameWorld *pGameWorld, vec2 Pos, int Owner, vec2 Direction, int Type, int Damage)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_ProximityRadius = 5.0f;
	m_Pos = Pos;
	m_Owner = Owner;
	m_Direction = Direction;
	m_Vel = vec2(0, 0);
	m_Type = Type;
	m_Damage = Damage;
	// Release timer defaults to half a second. Type 10 overrides with zero and
	// type 11 with 1.5 seconds below.
	m_ReleaseTimer = round_to_int(Server()->TickSpeed()*0.5f);
	m_LifeTimer = Server()->TickSpeed()*8;
	m_MoveTicks = 0;
	m_aOrbitPos[0] = m_aOrbitPos[1] = m_aOrbitPos[2] = Pos;

	AllocExtraIDs(2);
	if(m_Type == TYPE_HELI_BOMB)
		m_LifeTimer = Server()->TickSpeed()*8;
	else if(m_Type == TYPE_UBOAT_TORPEDO)
		m_ReleaseTimer = 0;
	else if(m_Type == TYPE_GRENADE)
	{
		m_ReleaseTimer = round_to_int(Server()->TickSpeed()*1.5f);
		m_Vel = length(Direction) > 0.0001f ? normalize(Direction)*9.0f : vec2(0, 0);
	}

	GameWorld()->InsertEntity(this);
}

void CBomb::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

bool CBomb::TouchesSolid(float Radius)
{
	CCollision *pCollision = GameServer()->Collision();
	return pCollision->CheckPoint(m_Pos.x+Radius, m_Pos.y) ||
		pCollision->CheckPoint(m_Pos.x-Radius, m_Pos.y) ||
		pCollision->CheckPoint(m_Pos.x, m_Pos.y-Radius) ||
		pCollision->CheckPoint(m_Pos.x, m_Pos.y+Radius);
}

void CBomb::ExplodeHeliBomb()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(pOwner)
		pOwner->ResetBattlefieldHeliBomb();
	const vec2 aOffsets[4] = {
		vec2(-50.0f, 0.0f), vec2(50.0f, 0.0f),
		vec2(50.0f, -70.0f), vec2(-50.0f, -70.0f)};
	for(int i = 0; i < 4; i++)
	{
		GameServer()->CreateExplosion(m_Pos+aOffsets[i], m_Owner, WEAPON_RIFLE, false);
		GameServer()->CreateDeath(m_Pos+aOffsets[i], m_Owner);
	}
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameServer()->m_World.DestroyEntity(this);
}

void CBomb::ExplodeTorpedo(bool DirectHit)
{
	if(m_Type == TYPE_GRENADE && DirectHit)
		GameServer()->CreateExplosion2(m_Pos, m_Owner, WEAPON_GRENADE, max(1, m_Damage));
	else
		GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_GRENADE, false);
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameServer()->m_World.DestroyEntity(this);
}

void CBomb::TickHeliBomb()
{
	if(m_Owner < 0)
		Reset();

	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pDirect = GameWorld()->ClosestCharacter(m_Pos, 30.0f, pOwner);
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_Pos, 1000.0f, pOwner);

	bool DoorCollide = GameServer()->IntersectBattlefieldDoor(m_Pos, m_Pos+vec2(1, 1));
	if(TouchesSolid(11.0f) || DoorCollide || (pDirect && pDirect->IsAlive()))
	{
		ExplodeHeliBomb();
		return;
	}

	if(pTarget && pTarget->IsAlive() && pTarget->GetPlayer() && pOwner &&
		pOwner->GetPlayer() &&
		pTarget->GetPlayer()->GetTeam() != pOwner->GetPlayer()->GetTeam() &&
		m_ReleaseTimer == 0 &&
		!GameServer()->Collision()->IntersectLine(m_Pos, pTarget->m_Pos, 0, 0))
	{
		vec2 ToTarget = pTarget->m_Pos-m_Pos;
		if(length(ToTarget) > 0.001f)
		{
			m_Direction = normalize(ToTarget)*13.0f;
			m_Pos += m_Direction;
			m_Vel = m_Direction;
		}
		GameServer()->CreateDeath(m_Pos, m_Owner);
		return;
	}

	if(m_Vel.x != 0.0f || m_Vel.y != 0.0f)
		m_Direction = m_Vel;
	m_Direction.y += 0.1f;
	m_Vel = m_Direction;
	if(GameServer()->Collision()->isWater(round_to_int(m_Pos.x), round_to_int(m_Pos.y)))
		m_Vel *= 0.1f;
	GameServer()->Collision()->MoveBox(&m_Pos, &m_Vel, vec2(5, 5), 0.1f);
	m_Direction = m_Vel;
}

void CBomb::TickTorpedo()
{
	if(m_Owner < 0)
		return;

	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_Pos, 30.0f, pOwner);
	if(pTarget && pTarget->IsAlive())
	{
		if(m_Type == TYPE_UBOAT_TORPEDO || m_Type == TYPE_SHIP_SHELL)
		{
			// The blast is conditional on a still-living owner, but the direct
			// two-point hit and sound are not.
			if(pOwner && pOwner->IsAlive())
				GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_GRENADE, false);
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
			pTarget->TakeDamage(vec2(0, 0), 2, m_Owner, WEAPON_GRENADE);
			GameServer()->m_World.DestroyEntity(this);
		}
		else if(m_Type == TYPE_GRENADE)
		{
			GameServer()->CreateExplosion2(m_Pos, m_Owner, WEAPON_GRENADE,
				max(1, m_Damage));
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
			GameServer()->m_World.DestroyEntity(this);
		}
	}

	// Type-11 checks the previous dynamic-movement count before incrementing
	// it later in this tick.
	if(m_MoveTicks > 29)
		ExplodeTorpedo(false);

	if(TouchesSolid(13.0f))
		ExplodeTorpedo(false);

	if(m_Type == TYPE_SHIP_SHELL)
		m_Vel.y = min(m_Vel.y+0.2f, 3.0f);
	else if(m_Type == TYPE_UBOAT_TORPEDO)
	{
		// Type 10 uses m_ReleaseTimer as its launch-age counter. Do not share
		// m_MoveTicks here: that belongs to the type-11 grenade path and would
		// make torpedoes explode after roughly 30 ticks.
		m_ReleaseTimer++;
		if(m_LifeTimer > 0)
			m_LifeTimer--;
		if(m_LifeTimer < round_to_int(Server()->TickSpeed()*1.5f))
			m_Vel.x *= 0.95f;
		if(m_LifeTimer == 0)
			ExplodeTorpedo(false);
		if(!GameServer()->Collision()->isWater(round_to_int(m_Pos.x), round_to_int(m_Pos.y)-50))
			m_Vel.y = 1.0f;
		if(m_ReleaseTimer <= 20)
			m_Vel.x += m_Direction.x > 0.0f ? 0.1f : -0.1f;
		else if(m_LifeTimer >= round_to_int(Server()->TickSpeed()*1.5f))
		{
			// Subtract 0.5f after the launch phase to partially offset the
			// surface-following y=1 assignment before horizontal acceleration.
			if(m_Vel.y > 0.0f)
				m_Vel.y -= 0.5f;
			m_Vel.x = clamp(m_Vel.x+
				(m_Direction.x > 0.0f ? 1.0f : -1.0f), -14.0f, 14.0f);
		}
	}
	else if(m_Type == TYPE_GRENADE)
	{
		if(m_ReleaseTimer > 0)
			m_ReleaseTimer--;
		else
		{
			m_MoveTicks++;
			m_Vel *= 0.9f;
		}
		if(!GameServer()->Collision()->isWater(round_to_int(m_Pos.x), round_to_int(m_Pos.y)))
			m_Vel.y += 1.0f;
	}

	GameServer()->Collision()->MoveBox(&m_Pos, &m_Vel, vec2(5, 5), 0.0f);
}

void CBomb::Tick()
{
	if(m_Type == TYPE_HELI_BOMB)
	{
		if(m_ReleaseTimer > 0)
			m_ReleaseTimer--;
		float Phase = Server()->Tick()*0.1f;
		// Snap assigns the base ID to the third calculated orbit point, then
		// the two reserved IDs to the first and second points.
		m_aOrbitPos[0] = m_Pos+GetDir(Phase-180.0f)*15.0f;
		m_aOrbitPos[1] = m_Pos+GetDir(Phase)*15.0f;
		m_aOrbitPos[2] = m_Pos+GetDir(Phase-90.0f)*15.0f;
		TickHeliBomb();
	}
	else
		TickTorpedo();
}

void CBomb::Snap(int SnappingClient)
{
	(void)SnappingClient;

	if(m_Type == TYPE_HELI_BOMB)
	{
		for(int i = 0; i < 3; i++)
		{
			int ID = i == 0 ? m_ID : m_aExtraIDs[i-1];
			CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(
				Server()->SnapNewItem(NETOBJTYPE_LASER, ID, sizeof(CNetObj_Laser)));
			if(!pLaser)
				return;
			pLaser->m_X = pLaser->m_FromX = round_to_int(m_aOrbitPos[i].x);
			pLaser->m_Y = pLaser->m_FromY = round_to_int(m_aOrbitPos[i].y);
			pLaser->m_StartTick = Server()->Tick();
		}
		return;
	}

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(
		Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
	if(!pProj)
		return;
	pProj->m_X = round_to_int(m_Pos.x);
	pProj->m_Y = round_to_int(m_Pos.y);
	pProj->m_VelX = 0;
	pProj->m_VelY = 0;
	pProj->m_Type = m_Type == TYPE_GRENADE ? WEAPON_SHOTGUN : WEAPON_GRENADE;
	pProj->m_StartTick = Server()->Tick()-1;
}
