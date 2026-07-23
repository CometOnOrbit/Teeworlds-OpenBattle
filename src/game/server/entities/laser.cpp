/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "laser.h"

CLaser::CLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner,
	int BattlefieldType)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Owner = Owner;
	m_BattlefieldType = BattlefieldType;
	m_BattlefieldLifeTimer = round_to_int(Server()->TickSpeed()*1.4f);
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = Server()->Tick();
	m_BattlefieldVel = vec2(0, 0);
	m_StickTimer = 0;
	m_AttachedCID = -1;
	m_From = Pos;

	if(m_BattlefieldType == 3)
	{
		if(m_Energy < 2.0f)
		{
			m_Energy = 2.0f;
			m_BattlefieldVel = Direction*6.0f;
		}
		else if(m_Energy <= 5.0f)
			m_BattlefieldVel = Direction*(m_Energy*3.0f);
		else
			m_BattlefieldVel = Direction*(m_Energy*2.0f);
	}
	else if(m_BattlefieldType == 4 || m_BattlefieldType == 5)
		m_BattlefieldVel = Direction*15.0f;

	if(m_BattlefieldType == 5)
		AllocExtraIDs(4);
	GameWorld()->InsertEntity(this);
	if(m_BattlefieldType == 1)
		DoBounce();
}


bool CLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *Hit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, OwnerChar);
	if(!Hit)
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	Hit->TakeDamage(vec2(0.f, 0.f), Hit->InBattlefieldVehicle() ? 2 : 9,
		m_Owner, WEAPON_RIFLE);
	return true;
}

void CLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;
	if(GameServer()->Collision()->isWater(round_to_int(To.x), round_to_int(To.y)))
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;

			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameServer()->Tuning()->m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum)
				m_Energy = -1;

			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_BOUNCE);
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}
}

void CLaser::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CLaser::Tick()
{
	if(m_BattlefieldType == 2)
	{
		TickBattlefieldBurstRound();
		return;
	}
	if(m_BattlefieldType >= 3)
	{
		TickBattlefieldWaterWeapon();
		return;
	}
	if(Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CLaser::TickBattlefieldBurstRound()
{
	if(m_BattlefieldLifeTimer > 0)
		m_BattlefieldLifeTimer--;

	vec2 Direction = length(m_Dir) > 0.0001f ? normalize(m_Dir) : vec2(1, 0);
	m_From = m_Pos-Direction*4.0f;
	m_Pos += m_Dir;
	m_Dir *= 1.067f;

	bool DoorCollide = GameServer()->IntersectBattlefieldDoor(m_Pos, m_Pos+vec2(1, 1));
	if(m_BattlefieldLifeTimer == 0 || DoorCollide ||
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y))
	{
		GameServer()->CreateDamageInd(m_Pos, -GetAngle(m_Dir), 1);
		GameWorld()->DestroyEntity(this);
	}

	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_Pos, 30.0f, pOwner);
	if(pTarget && pTarget->IsAlive() &&
		!(pTarget->IsBattlefieldVehiclePassenger() &&
		  pTarget->GetBattlefieldVehicleType() == CCharacter::BATTLEFIELD_VEHICLE_HELI))
	{
		pTarget->TakeDamage(m_Dir, 2, m_Owner, WEAPON_SHOTGUN);
		GameWorld()->DestroyEntity(this);
	}
}

void CLaser::TickBattlefieldWaterWeapon()
{
	if(m_AttachedCID >= 0)
	{
		CCharacter *pAttached = GameServer()->GetPlayerChar(m_AttachedCID);
		if(!pAttached || !pAttached->IsAlive())
		{
			GameWorld()->DestroyEntity(this);
			return;
		}
		m_From = m_Pos;
		m_Pos = pAttached->m_Pos-normalize(m_Dir)*30.0f;
	}

	if(m_StickTimer > 0)
	{
		m_StickTimer--;
		if(m_StickTimer == 1)
			GameWorld()->DestroyEntity(this);
	}

	if(m_AttachedCID >= 0)
		return;

	bool InWater = GameServer()->Collision()->isWater(round_to_int(m_Pos.x), round_to_int(m_Pos.y));
	m_BattlefieldVel.y += InWater ?
		(m_BattlefieldType == 3 ? 0.1f : 0.05f) :
		(m_BattlefieldType == 3 ? 0.3f : 0.2f);

	vec2 OldPos = m_Pos;
	bool Collide =
		GameServer()->Collision()->CheckPoint(m_Pos.x+13.0f, m_Pos.y) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x-13.0f, m_Pos.y) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y-13.0f) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y+13.0f);
	// Like CLaser::Tick's door loop, test the current position using
	// its synthetic one-pixel segment before applying this tick's movement.
	bool DoorCollide = GameServer()->IntersectBattlefieldDoor(
		OldPos, OldPos+vec2(1, 1));
	if(!Collide && !DoorCollide)
	{
		float Elasticity = m_BattlefieldType == 3 ? 0.1f : 0.05f;
		GameServer()->Collision()->MoveBox(
			&m_Pos, &m_BattlefieldVel, vec2(1, 1), Elasticity);
	}
	m_From = OldPos;
	if(length(m_BattlefieldVel) > 0.001f)
		m_Dir = normalize(m_BattlefieldVel);

	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	float Radius = m_BattlefieldType == 5 ? 60.0f : 30.0f;
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_Pos, Radius, pOwner);
	if(pTarget && pTarget->IsAlive())
	{
		int Damage = 2;
		if(m_BattlefieldType == 3)
			Damage = m_Energy > 9.0f ? 10 : (m_Energy <= 5.0f ? 6 : 8);
		else if(m_BattlefieldType == 5)
			Damage = 9;
		pTarget->TakeDamage(vec2(0, 0), Damage, m_Owner, WEAPON_RIFLE);
		m_AttachedCID = pTarget->GetPlayer()->GetCID();
		m_BattlefieldVel = vec2(0, 0);
		return;
	}

	if(Collide || DoorCollide)
	{
		m_BattlefieldVel = vec2(0, 0);
		m_StickTimer = Server()->TickSpeed()*10;
	}
}

void CLaser::SnapLaserObject(int ID, vec2 From, vec2 To, int StartTick)
{
	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(
		Server()->SnapNewItem(NETOBJTYPE_LASER, ID, sizeof(CNetObj_Laser)));
	if(!pObj)
		return;
	pObj->m_X = round_to_int(To.x);
	pObj->m_Y = round_to_int(To.y);
	pObj->m_FromX = round_to_int(From.x);
	pObj->m_FromY = round_to_int(From.y);
	pObj->m_StartTick = StartTick;
}

void CLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;
	if(m_BattlefieldType == 2)
	{
		CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(
			Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
		if(pProj)
		{
			pProj->m_X = round_to_int(m_Pos.x);
			pProj->m_Y = round_to_int(m_Pos.y);
			pProj->m_VelX = pProj->m_VelY = 0;
			pProj->m_Type = WEAPON_SHOTGUN;
			pProj->m_StartTick = Server()->Tick()-1;
		}
		return;
	}

	if(m_BattlefieldType >= 3)
	{
		if(m_BattlefieldType == 3)
		{
			vec2 RoundedPos(round_to_int(m_Pos.x), round_to_int(m_Pos.y));
			vec2 To = RoundedPos+m_Dir*(m_AttachedCID >= 0 ? 1.0f : 15.0f);
			vec2 From = RoundedPos-m_Dir*170.0f;
			SnapLaserObject(m_ID, From, To, Server()->Tick()-3);
		}
		else if(m_BattlefieldType == 4)
		{
			vec2 RoundedPos(round_to_int(m_Pos.x), round_to_int(m_Pos.y));
			SnapLaserObject(m_ID, RoundedPos-m_Dir*50.0f, RoundedPos,
				Server()->Tick()-3);
		}
		else if(m_BattlefieldType == 5)
		{
			vec2 A;
			vec2 B;
			vec2 C;
			vec2 D;
			if(m_AttachedCID >= 0)
			{
				vec2 Vertical(0.3716656f, 29.9976978f);
				A = m_Pos-Vertical;
				B = m_Pos+Vertical;
				C = m_Pos+vec2(30, 0);
				D = m_Pos-vec2(30, 0);
			}
			else
			{
				float Phase = Server()->Tick()*0.2f;
				vec2 AxisA = GetDir(Phase+4.7f)*30.0f;
				vec2 AxisB = GetDir(Phase)*30.0f;
				A = m_Pos+AxisA;
				B = m_Pos-AxisA;
				C = m_Pos+AxisB;
				D = m_Pos-AxisB;
			}
			SnapLaserObject(m_aExtraIDs[0], A, B, Server()->Tick()-3);
			SnapLaserObject(m_aExtraIDs[1], B, B, Server()->Tick()-3);
			SnapLaserObject(m_aExtraIDs[2], C, D, Server()->Tick()-3);
			SnapLaserObject(m_aExtraIDs[3], D, D, Server()->Tick()-3);
		}
		return;
	}

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_From.x;
	pObj->m_FromY = (int)m_From.y;
	pObj->m_StartTick = m_EvalTick;
}
