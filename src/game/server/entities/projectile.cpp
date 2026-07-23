/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "bomb.h"
#include "projectile.h"

CProjectile::CProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
		int Damage, bool Explosive, float Force, int SoundImpact, int Weapon)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_Type = Type;
	m_Pos = Pos;
	m_Direction = Dir;
	m_LifeSpan = Span;
	m_Owner = Owner;
	m_Force = Force;
	m_Damage = Damage;
	m_SoundImpact = SoundImpact;
	m_Weapon = Weapon;
	m_StartTick = Server()->Tick();
	m_Explosive = Explosive;
	m_CustomType = 0;
	m_CustomTick = 0;
	GameWorld()->InsertEntity(this);
}

CProjectile::CProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
		int Damage, bool Explosive, int CustomType, float Force, int SoundImpact, int Weapon)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_Type = Type;
	m_Pos = Pos;
	m_Direction = Dir;
	m_LifeSpan = Span;
	m_Owner = Owner;
	m_Force = Force;
	m_Damage = Damage;
	m_SoundImpact = SoundImpact;
	m_Weapon = Weapon;
	m_StartTick = Server()->Tick();
	m_Explosive = Explosive;
	m_CustomType = CustomType;
	m_CustomTick = 0;

	GameWorld()->InsertEntity(this);
}

void CProjectile::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

vec2 CProjectile::GetPos(float Time)
{
	const CTuneParam *pCurvature = 0;
	const CTuneParam *pSpeed = 0;

	switch(m_Type)
	{
		case WEAPON_GRENADE:
			pCurvature = &GameServer()->Tuning()->m_GrenadeCurvature;
			pSpeed = &GameServer()->Tuning()->m_GrenadeSpeed;
			break;

		case WEAPON_SHOTGUN:
			pCurvature = &GameServer()->Tuning()->m_ShotgunCurvature;
			pSpeed = &GameServer()->Tuning()->m_ShotgunSpeed;
			break;

		case WEAPON_GUN:
			pCurvature = &GameServer()->Tuning()->m_GunCurvature;
			pSpeed = &GameServer()->Tuning()->m_GunSpeed;
			break;
	}

	long double Curvature = pCurvature ? (long double)pCurvature->Get()/100.0L : 0.0L;
	long double Speed = pSpeed ? (long double)pSpeed->Get()/100.0L : 0.0L;
	long double ScaledTime = (long double)Time*Speed;
	return vec2(
		(float)((long double)m_Pos.x + (long double)m_Direction.x*ScaledTime),
		(float)((long double)m_Pos.y + (long double)m_Direction.y*ScaledTime +
			(Curvature/10000.0L)*(ScaledTime*ScaledTime)));
}


void CProjectile::Tick()
{
	if(m_CustomType == 6)
		m_CustomTick++;

	float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, &CurPos, 0);
	bool DoorCollide = GameServer()->IntersectBattlefieldDoor(PrevPos, CurPos);
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *TargetChr = GameServer()->m_World.IntersectCharacter(PrevPos, CurPos, 6.0f, CurPos, OwnerChar);

	// Anti-aircraft rounds exist only while their gunner is still present and
	// leave a death marker along their complete flight path.
	if(m_CustomType == 4)
	{
		if(!OwnerChar)
			GameServer()->m_World.DestroyEntity(this);
		else
			GameServer()->CreateDeath(CurPos, m_Owner);
	}

	// Type 30 is the short-range execution projectile used by the
	// server. It tests a wider 30-unit swept radius and bypasses normal damage.
	if(m_CustomType == 30)
	{
		vec2 HitPos;
		CCharacter *pHit = GameServer()->m_World.IntersectCharacter(
			PrevPos, CurPos, 30.0f, HitPos, 0);
		if(pHit)
			pHit->Die(m_Owner, m_Weapon);
		return;
	}

	if(GameServer()->m_BattlefieldWaterEnabled &&
		GameServer()->Collision()->isWater(round_to_int(CurPos.x), round_to_int(CurPos.y)))
	{
		if(m_CustomType == 8)
			new CBomb(GameWorld(), CurPos, m_Owner, vec2(1, 1), CBomb::TYPE_SHIP_SHELL, 1);
		else
		{
			if(m_CustomType == 7 && OwnerChar)
				OwnerChar->ResetBattlefieldUboatShot();
			GameServer()->CreateDamageInd(CurPos, GetAngle(m_Direction), 1);
		}
		GameServer()->m_World.DestroyEntity(this);
		if(m_CustomType == 7)
			return;
	}

	if(m_CustomType == 7 && OwnerChar && !OwnerChar->BattlefieldUboatShotArmed())
	{
		GameServer()->CreateExplosion2(CurPos, m_Owner, m_Weapon, 1);
		for(int i = -30; i <= 30; i++)
		{
			float Angle = GetAngle(m_Direction)+i*0.1f;
			float v = 1.0f-absolute(i)/30.0f;
			float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff,
				1.0f, v);
			new CProjectile(GameWorld(), WEAPON_SHOTGUN, m_Owner, PrevPos,
				GetDir(Angle)*Speed, max(1, round_to_int(Server()->TickSpeed()*0.1f)),
				1, false, 1, 0.0f, -1, WEAPON_SHOTGUN);
		}
		GameServer()->CreateSound(PrevPos, m_SoundImpact);
		GameServer()->m_World.DestroyEntity(this);
	}

	m_LifeSpan--;

	if(TargetChr || Collide || DoorCollide || m_LifeSpan < 0 || GameLayerClipped(CurPos))
	{
		if(m_CustomType == 7 && OwnerChar)
			OwnerChar->ResetBattlefieldUboatShot();
		if(m_LifeSpan >= 0 || m_Weapon == WEAPON_GRENADE)
			GameServer()->CreateSound(CurPos, m_SoundImpact);
		if(m_CustomType == 22 && (TargetChr || Collide || DoorCollide))
			GameServer()->CreateDamageInd(CurPos, -GetAngle(m_Direction), 10);

		if(m_Explosive)
		{
			if(m_CustomType == 2)
			{
				const vec2 aOffsets[4] = {vec2(-50, 0), vec2(50, 0), vec2(50, -70), vec2(-50, -70)};
				for(int i = 0; i < 4; i++)
				{
					GameServer()->CreateExplosion(CurPos+aOffsets[i], m_Owner, m_Weapon, false);
					GameServer()->CreateDeath(CurPos+aOffsets[i], m_Owner);
				}
			}
			else if(m_CustomType == 3 || m_CustomType == 7)
			{
				GameServer()->CreateExplosion2(CurPos, m_Owner, m_Weapon, 1);
				if(m_CustomType == 3)
					GameServer()->CreateSound(CurPos, SOUND_GRENADE_EXPLODE);
			}
			else if(m_CustomType == 8)
			{
				GameServer()->CreateExplosion2(CurPos, m_Owner, m_Weapon, 1);
				GameServer()->CreateSound(CurPos, SOUND_GRENADE_EXPLODE);
			}
			else if(m_CustomType == 23 && (Collide || DoorCollide))
			{
				float ImpactAngle = GetAngle(normalize(CurPos-m_Pos));
				for(int i = -30; i <= 30; i++)
				{
					float Angle = ImpactAngle+i*0.1f;
					float v = 1.0f-absolute(i)/30.0f;
					float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff,
						1.0f, v);
					new CProjectile(GameWorld(), WEAPON_SHOTGUN, m_Owner, PrevPos,
						GetDir(Angle)*Speed, max(1, Server()->TickSpeed()/10), 1, false, 1,
						0.0f, -1, WEAPON_SHOTGUN);
				}
				GameServer()->CreateSound(PrevPos, SOUND_HOOK_NOATTACH);
			}
			else
			{
				GameServer()->CreateExplosion(CurPos, m_Owner, m_Weapon, false);
				if(m_CustomType == 1 && m_Weapon == WEAPON_SHOTGUN)
					GameServer()->CreateSound(CurPos, SOUND_GRENADE_EXPLODE);
			}
		}

		else if(TargetChr)
		{
			bool AntiAircraftVehicleHit = m_CustomType == 4 && OwnerChar &&
				OwnerChar->GetPlayer() && TargetChr->GetPlayer() &&
				OwnerChar->GetPlayer()->GetTeam() != TargetChr->GetPlayer()->GetTeam() &&
				(TargetChr->GetBattlefieldVehicleType() == CCharacter::BATTLEFIELD_VEHICLE_HELI ||
				 TargetChr->GetBattlefieldVehicleType() == CCharacter::BATTLEFIELD_VEHICLE_JET);
			if(AntiAircraftVehicleHit)
				TargetChr->TakeDamage(vec2(0, 0), 5, m_Owner, m_Weapon);
			else
			{
				float Force = m_CustomType == 22 ? 20.0f : max(0.001f, m_Force);
				TargetChr->TakeDamage(m_Direction*Force, m_Damage, m_Owner, m_Weapon);
			}
		}

		// Medic shotgun pellets split once on solid geometry. The child pellet
		// deliberately survives its first five wall contacts/ticks (custom type 6).
		if(m_CustomType == 5 && (Collide || DoorCollide))
		{
			new CProjectile(GameWorld(), WEAPON_SHOTGUN, m_Owner, CurPos,
				m_Direction*0.4f,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime*10.0f),
				1, false, 6, 0.0f, -1, WEAPON_SHOTGUN);
			GameServer()->CreateSound(CurPos, SOUND_RIFLE_BOUNCE);
			GameServer()->CreateDamageInd(CurPos, GetAngle(m_Direction), 1);
		}

		if(m_CustomType == 6 && (Collide || DoorCollide) && m_CustomTick < 6)
			return;
		if(m_CustomType == 7)
		{
			GameServer()->CreateDamageInd(CurPos, GetAngle(m_Direction), 1);
			GameServer()->CreateSound(PrevPos, SOUND_PICKUP_ARMOR);
		}

		GameServer()->m_World.DestroyEntity(this);
	}
}

void CProjectile::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	// Truncate direction*100 from long double so values like 69.99999 stay 69
	// instead of rounding to float 70 first.
	pProj->m_VelX = (int)((long double)m_Direction.x*(long double)100.0f);
	pProj->m_VelY = (int)((long double)m_Direction.y*(long double)100.0f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = m_Type;
}

void CProjectile::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();

	if(NetworkClipped(SnappingClient, GetPos(Ct)))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
	if(pProj)
		FillInfo(pProj);
}
