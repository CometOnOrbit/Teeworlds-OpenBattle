#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include "character.h"
#include "guided_projectile.h"

CGuidedProjectile::CGuidedProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir,
	int Span, int Damage, bool Explosive, int CustomType, float Force,
	int SoundImpact, int Weapon)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_Type = Type;
	m_Owner = Owner;
	m_Pos = Pos;
	m_Direction = Dir;
	m_Vel = vec2(0, 0);
	m_LifeSpan = Span;
	m_Damage = Damage;
	m_Explosive = Explosive;
	m_CustomType = CustomType;
	m_Force = Force;
	m_SoundImpact = SoundImpact;
	m_Weapon = Weapon;
	m_ProximityRadius = 1.0f;
	m_Ballistic = false;
	m_HasTarget = false;
	m_SavePosition = false;
	m_SavedPos = Pos;
	AllocExtraIDs(1);
	GameWorld()->InsertEntity(this);
}

void CGuidedProjectile::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CGuidedProjectile::Snap(int SnappingClient)
{
	(void)SnappingClient;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(
		Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
	if(!pProj)
		return;
	pProj->m_X = round_to_int(m_Pos.x);
	pProj->m_Y = round_to_int(m_Pos.y);
	pProj->m_VelX = round_to_int(m_Vel.x);
	pProj->m_VelY = round_to_int(m_Vel.y);
	pProj->m_Type = WEAPON_GRENADE;
	pProj->m_StartTick = Server()->Tick()-1;

	if(m_HasTarget)
	{
		pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(
			NETOBJTYPE_PROJECTILE, m_aExtraIDs[0], sizeof(CNetObj_Projectile)));
		if(!pProj)
			return;
		pProj->m_X = round_to_int(m_Pos.x)+25;
		pProj->m_Y = round_to_int(m_Pos.y)-25;
		pProj->m_VelX = round_to_int(m_Vel.x);
		pProj->m_VelY = round_to_int(m_Vel.y);
		pProj->m_Type = WEAPON_GUN;
		pProj->m_StartTick = Server()->Tick();
	}
}

void CGuidedProjectile::Impact(CCharacter *pTarget, bool Explode)
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(pOwner)
		pOwner->ResetEngineerGrenadeControlState();
	if(Explode && m_Explosive)
		GameServer()->CreateExplosion(m_Pos, m_Owner, m_Weapon, false);
	if(Explode && m_SoundImpact >= 0)
		GameServer()->CreateSound(m_Pos, m_SoundImpact);
	if(pTarget && pTarget->IsAlive())
	{
		float Force = max(0.001f, m_Force);
		pTarget->TakeDamage(m_Direction*Force,
			m_CustomType == 8 || m_CustomType == 9 ? 9 : m_Damage,
			m_Owner, m_Weapon);
	}
	GameWorld()->DestroyEntity(this);
}

void CGuidedProjectile::Tick()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	m_LifeSpan--;
	if(GameServer()->m_BattlefieldWaterEnabled &&
		GameServer()->Collision()->isWater(round_to_int(m_Pos.x), round_to_int(m_Pos.y)))
	{
		pOwner->ResetEngineerGrenadeControlState();
		GameServer()->CreateDamageInd(m_Pos, GetAngle(m_Direction), 1);
		GameWorld()->DestroyEntity(this);
	}

	bool Solid = GameServer()->Collision()->CheckPoint(m_Pos.x+13.0f, m_Pos.y) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x-13.0f, m_Pos.y) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y-13.0f) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y+13.0f);
	bool DoorCollide = GameServer()->IntersectBattlefieldDoor(
		m_Pos, m_Pos+vec2(1, 1));
	if(Solid || DoorCollide)
		Impact(0, true);
	else
	{
		CCharacter *pHit = GameWorld()->ClosestCharacter(m_Pos, 12.0f, pOwner);
		if(pHit && pHit->IsAlive())
			Impact(pHit, false);
	}

	if(m_CustomType == 8 || m_CustomType == 9)
	{
		bool HasControl = (m_CustomType == 8 && pOwner->GetEngineerGrenadeControlState()) ||
			(m_CustomType == 9 && !pOwner->GetEngineerGrenadeControlState());
		if(!HasControl)
			m_Ballistic = true;

		if(m_Ballistic)
		{
			m_HasTarget = false;
			m_Vel.y += 0.5f;
			GameServer()->Collision()->MoveBox(&m_Pos, &m_Vel, vec2(1, 1), 0.5f);
			return;
		}
		else
		{
			CCharacter *pTarget = GameWorld()->ClosestCharacter(m_Pos, 300.0f, pOwner);
			if(pTarget && pTarget->IsAlive() && pTarget->GetPlayer())
			{
				if(pTarget->GetPlayer()->GetTeam() != pOwner->GetPlayer()->GetTeam())
					m_Vel = normalize(pTarget->m_Pos-m_Pos)*13.0f;
				m_HasTarget = true;
			}
			else
			{
				vec2 Aim = pOwner->GetEngineerGrenadeAimPoint()-m_Pos;
				if(length(Aim) > 0.001f)
				{
					vec2 Direction = normalize(Aim);
					m_Vel += Direction*2.1f;
				}
				m_HasTarget = false;
			}
			if(length(m_Pos-m_SavedPos) > 20.0f)
				m_Vel *= 0.8f;
			if(!m_SavePosition)
			{
				m_SavedPos = m_Pos;
				m_SavePosition = true;
			}
			else
				m_SavePosition = false;
			GameServer()->Collision()->MoveBox(&m_Pos, &m_Vel, vec2(1, 1), 0.0f);
			return;
		}
	}
}
