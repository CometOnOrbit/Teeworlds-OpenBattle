#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "c4.h"
#include "character.h"

CC4::CC4(CGameWorld *pGameWorld, vec2 Pos, int Owner, vec2 Vel)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_CurrentPos = Pos;
	m_Pos = Pos;
	m_InitialVel = Vel;
	m_CurrentVel = vec2(0, 0);
	m_Owner = Owner;
	m_CollisionLatched = false;
	m_Attached = false;
	m_Defusing = false;
	m_DefuseProgress = 0;
	m_DefuserPos = vec2(0, 0);
	m_ProximityRadius = 9.0f;
	AllocExtraIDs(1);
	GameWorld()->InsertEntity(this);
}

void CC4::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CC4::SnapLaser(int ID, vec2 From, vec2 To)
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

void CC4::Snap(int SnappingClient)
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pViewer = GameServer()->GetPlayerChar(SnappingClient);
	if(!pOwner || !pViewer)
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(
		Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
	if(pProj)
	{
		pProj->m_X = round_to_int(m_CurrentPos.x);
		pProj->m_Y = round_to_int(m_CurrentPos.y);
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		pProj->m_Type = WEAPON_RIFLE;
		pProj->m_StartTick = Server()->Tick() -
			(pOwner->GetPlayer()->GetTeam() == pViewer->GetPlayer()->GetTeam() ? 0 : 3);
	}

	if(m_Defusing)
		SnapLaser(m_aExtraIDs[0], m_CurrentPos, m_DefuserPos);
}

void CC4::Explode()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
		return;

	CCharacter *pTarget = m_Attached ?
		GameWorld()->ClosestCharacter(m_CurrentPos, 30.0f, pOwner) : 0;
	GameServer()->CreateExplosion(m_CurrentPos, m_Owner, WEAPON_RIFLE, false);
	GameServer()->CreateSound(m_CurrentPos, SOUND_GRENADE_EXPLODE);
	pOwner->ReleaseC4Slot();
	if(pTarget && pTarget->IsAlive())
		pTarget->TakeDamage(vec2(0, 0), 10, m_Owner, WEAPON_HAMMER);
	GameWorld()->DestroyEntity(this);
}

void CC4::Tick()
{
	// Owner -1 means the charge is disabled: skip defuse progress and physics.
	if(m_Owner == -1)
		return;

	m_Defusing = false;
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pDefuser = GameWorld()->ClosestCharacter(m_CurrentPos, 200.0f, 0);
	if(!pOwner)
		GameWorld()->DestroyEntity(this);

	if(pOwner && pDefuser && pDefuser->IsAlive() &&
		pDefuser->GetPlayer()->GetTeam() != pOwner->GetPlayer()->GetTeam() &&
		pDefuser->GetPlayer()->IsEngineer() && pDefuser->IsEngineerAction() &&
		!GameServer()->Collision()->IntersectLine(m_CurrentPos, pDefuser->m_Pos, 0, 0))
	{
		m_Defusing = true;
		m_DefuserPos = pDefuser->m_Pos;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Defuse C4: %i | 70", pDefuser->GetPlayer()->GetCID()), m_DefuseProgress);
		GameServer()->SendBroadcast(aBuf, pDefuser->GetPlayer()->GetCID());
		pDefuser->SetBattlefieldBroadcastTimer(20);
		if(m_DefuseProgress > 69)
		{
			pOwner->ReleaseC4Slot();
			GameServer()->CreateExplosion2(m_CurrentPos, m_Owner, WEAPON_RIFLE, 1);
			GameServer()->CreateSound(m_CurrentPos, SOUND_GRENADE_EXPLODE);
			GameWorld()->DestroyEntity(this);
		}
	}

	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_CurrentPos, 30.0f, pOwner);

	// Door contact is recomputed every tick.  Solid tiles latch collision only
	// while the charge is not attached; this odd distinction is observable when
	// an attached target moves through a wall.
	m_CollisionLatched = GameServer()->IntersectBattlefieldDoor(
		m_CurrentPos, m_CurrentPos+vec2(1, 1));
	bool SkipMovement = false;
	bool Solid =
		GameServer()->Collision()->CheckPoint(m_CurrentPos.x+13.0f, m_CurrentPos.y) ||
		GameServer()->Collision()->CheckPoint(m_CurrentPos.x-13.0f, m_CurrentPos.y) ||
		GameServer()->Collision()->CheckPoint(m_CurrentPos.x, m_CurrentPos.y-13.0f) ||
		GameServer()->Collision()->CheckPoint(m_CurrentPos.x, m_CurrentPos.y+13.0f);
	if(Solid && !m_Attached)
	{
		m_CollisionLatched = true;
		SkipMovement = true;
	}

	if(!SkipMovement)
	{
		if(!pTarget)
		{
			if(m_Attached)
			{
				if(!m_CollisionLatched)
				{
					m_Attached = false;
					m_InitialVel = vec2(0, 0);
				}
				SkipMovement = true;
			}
		}
		else if(pTarget->IsAlive() && !m_CollisionLatched)
		{
			// No target CID is retained.  Each tick the closest live character is
			// selected again and the offset is rebuilt from the dynamic velocity.
			vec2 Direction = length(m_CurrentVel) > 0.001f ?
				normalize(m_CurrentVel) : vec2(0, 0);
			m_Attached = true;
			m_CurrentPos = pTarget->m_Pos-Direction*30.0f;
			SkipMovement = true;
		}
		else if(m_Attached)
			SkipMovement = true;
	}

	if(!SkipMovement && !m_CollisionLatched)
	{
		vec2 MoveVel = m_InitialVel;
		if(m_CurrentVel.y != 0.0f)
			MoveVel = m_CurrentVel;
		if(GameServer()->Collision()->isWater(round_to_int(m_CurrentPos.x), round_to_int(m_CurrentPos.y)))
		{
			if(m_CurrentVel.x > 0.0f)
				MoveVel.x -= 0.2f;
			else if(m_CurrentVel.x < 0.0f)
				MoveVel.x += 0.2f;
			if(m_CurrentVel.y > 3.0f)
				MoveVel.y -= 1.0f;
			else if(m_CurrentVel.y < 0.0f)
				MoveVel.y += 1.0f;
			MoveVel.y += 0.1f;
		}
		else
			MoveVel.y += GameServer()->Tuning()->m_Gravity;
		m_CurrentVel = MoveVel;
		GameServer()->Collision()->MoveBox(
			&m_CurrentPos, &m_CurrentVel, vec2(8, 8), 0.7f);
	}

	m_Pos = m_CurrentPos;

	// Hit and defuse progress are deliberately last. DestroyEntity only queues
	// removal, so completion/detonation still leaves these same-tick writes.
	if(pOwner && pOwner->WantsC4Detonation())
		Explode();
	if(m_Defusing)
		m_DefuseProgress++;
	else if(m_DefuseProgress > 0)
		m_DefuseProgress--;
}
