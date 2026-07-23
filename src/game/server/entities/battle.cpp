#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "battle.h"
#include "character.h"
#include "projectile.h"

int CBattle::MaxHealth(int Type)
{
	if(Type == TYPE_HELI || Type == TYPE_JET)
		return 10;
	if(Type == TYPE_PANZER || Type == TYPE_UBOAT)
		return 35;
	if(Type == TYPE_CAR)
		return 16;
	if(Type == TYPE_SHIP)
		return 25;
	if(Type == TYPE_MINI)
		return 5;
	return 10;
}

const char *CBattle::TypeName(int Type)
{
	switch(Type)
	{
	case TYPE_HELI: return "Heli";
	case TYPE_JET: return "Jet";
	case TYPE_PANZER: return "Panzer";
	case TYPE_CAR: return "Car";
	case TYPE_UBOAT: return "U-boat";
	case TYPE_SHIP: return "Ship";
	case TYPE_MINI: return "Mini";
	default: return "Vehicle";
	}
}

CBattle::CBattle(CGameWorld *pGameWorld, vec2 Pos, int Type, int KeyTeam)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_ProximityRadius = 14.0f;
	m_SpawnPos = Pos;
	m_PhysicalPos = Pos;
	m_Pos = Pos;
	m_Vel = vec2(0, 0);
	m_pDriver = 0;
	m_Type = Type;
	m_KeyTeam = KeyTeam;
	m_Health = MaxHealth(Type);
	m_RespawnTimer = 0;
	m_EnterCooldown = 0;
	m_RepairCooldown = 0;
	m_MessageCooldown = 0;
	m_LastDriverCID = -1;
	m_JetSafelyStopped = false;
	m_Repairing = false;
	m_RepairerPos = Pos;
	m_SnapshotTilt = 0.0f;
	m_SnapshotTiltCooldown = 0;
	AllocExtraIDs(9);
	GameWorld()->InsertEntity(this);
}

void CBattle::ResetToSpawn(bool ApplyEnterCooldown)
{
	m_Pos = m_SpawnPos;
	m_PhysicalPos = m_SpawnPos;
	m_Vel = vec2(0, 0);
	m_Health = MaxHealth(m_Type);
	m_RespawnTimer = 0;
	m_EnterCooldown = ApplyEnterCooldown ? Server()->TickSpeed()*3 : 0;
	m_RepairCooldown = 0;
	m_Repairing = false;
	m_SnapshotTilt = 0.0f;
	m_SnapshotTiltCooldown = 0;
}

void CBattle::Reset()
{
	if(m_pDriver)
		m_pDriver->DetachBattlefieldVehicle(this);
	m_pDriver = 0;
	ResetToSpawn();
}

bool CBattle::TeamCanUse(const CCharacter *pCharacter) const
{
	if(!pCharacter || !pCharacter->GetPlayer())
		return false;
	if(m_KeyTeam == 1)
		return pCharacter->GetPlayer()->GetTeam() == TEAM_RED;
	if(m_KeyTeam == 2)
		return pCharacter->GetPlayer()->GetTeam() == TEAM_BLUE;
	return true;
}

void CBattle::TryRepair()
{
	m_Repairing = false;
	if(m_Type == TYPE_MINI || m_Health >= MaxHealth(m_Type))
		return;

	// ClosestCharacter can return an infantry player who is not repairing and
	// thereby hide a valid engineer standing just behind them. Select the
	// nearest eligible engineer instead. TeamCanUse keeps team-keyed vehicles
	// repairable by their own team without allowing the opposing team to
	// maintain them.
	CCharacter *apCharacters[MAX_CLIENTS];
	int Num = GameWorld()->FindEntities(m_Pos, 200.0f,
		(CEntity **)apCharacters, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	CCharacter *pRepairer = 0;
	float ClosestDistance = 200.0f;
	for(int i = 0; i < Num; i++)
	{
		CCharacter *pCandidate = apCharacters[i];
		if(!pCandidate || !pCandidate->IsAlive() || !pCandidate->GetPlayer() ||
			!pCandidate->GetPlayer()->IsEngineer() ||
			!pCandidate->IsEngineerAction() || !TeamCanUse(pCandidate) ||
			GameServer()->Collision()->IntersectLine(
				m_Pos, pCandidate->m_Pos, 0, 0))
			continue;
		float CandidateDistance = distance(m_Pos, pCandidate->m_Pos);
		if(!pRepairer || CandidateDistance < ClosestDistance)
		{
			pRepairer = pCandidate;
			ClosestDistance = CandidateDistance;
		}
	}
	if(!pRepairer)
		return;

	m_Repairing = true;
	m_RepairerPos = pRepairer->m_Pos;
	m_RespawnTimer = Server()->TickSpeed()*10;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Vehicle-Health: %i | %i", m_Health, MaxHealth(m_Type));
	GameServer()->SendBroadcast(aBuf, pRepairer->GetPlayer()->GetCID());
	pRepairer->SetBattlefieldBroadcastTimer(20);
	if(m_RepairCooldown == 0)
	{
		m_Health = min(MaxHealth(m_Type), m_Health+3);
		m_RepairCooldown = Server()->TickSpeed();
		GameServer()->CreatePlayerSpawn(m_Pos);
	}
}

void CBattle::TryEnter()
{
	if(m_EnterCooldown > 0)
		return;

	CCharacter *pCharacter = GameWorld()->ClosestCharacter(m_Pos, 20.0f, 0);
	if(!pCharacter || !pCharacter->CanEnterBattlefieldVehicle())
		return;

	if(!TeamCanUse(pCharacter))
	{
		if(m_MessageCooldown == 0)
		{
			GameServer()->SendChatTarget(pCharacter->GetPlayer()->GetCID(),
				"You don't have the key for this vehicle!");
			m_MessageCooldown = Server()->TickSpeed()*2;
		}
		return;
	}

	if(pCharacter->EnterBattlefieldVehicle(this, m_Type, m_Health))
	{
		m_pDriver = pCharacter;
		m_RespawnTimer = 0;
		m_PhysicalPos = pCharacter->m_Pos;
		m_Pos = pCharacter->m_Pos;
		GameServer()->CreateSound(m_Pos,
			m_Type == TYPE_JET ? SOUND_PICKUP_SHOTGUN : SOUND_PICKUP_GRENADE);
	}
}

void CBattle::OnDriverLeft(CCharacter *pDriver, bool Destroyed, bool ImmediateReset)
{
	if(pDriver != m_pDriver)
		return;

	m_Health = clamp(pDriver->GetBattlefieldVehicleHealth(), 0, MaxHealth(m_Type));
	m_Vel = pDriver->GetBattlefieldVehicleVelocity();
	m_LastDriverCID = pDriver->GetPlayer() ? pDriver->GetPlayer()->GetCID() : -1;
	m_JetSafelyStopped = pDriver->BattlefieldJetSafelyStopped();
	m_PhysicalPos = pDriver->m_Pos;
	m_Pos = m_PhysicalPos;
	m_pDriver = 0;
	if(Destroyed || ImmediateReset)
	{
		ResetToSpawn();
		return;
	}
	m_Vel *= m_Type == TYPE_JET ? 0.3f : 0.5f;
	m_RespawnTimer = Server()->TickSpeed()*10;
	// Abandoned vehicles stay immediately reclaimable. The three-second entry
	// lock is armed only when the ten-second abandonment returns it to spawn.
	m_EnterCooldown = 0;
}

void CBattle::CrashAbandonedJet()
{
	m_RespawnTimer = 0;
	if(m_LastDriverCID >= 0 && m_LastDriverCID < MAX_CLIENTS &&
		GameServer()->m_apPlayers[m_LastDriverCID])
	{
		GameServer()->CreateExplosion(m_PhysicalPos, m_LastDriverCID, WEAPON_RIFLE, false);
		GameServer()->CreateSound(m_PhysicalPos, SOUND_GRENADE_EXPLODE);
		for(int i = -30; i <= 30; i++)
		{
			float v = 1.0f-absolute(i)/30.0f;
			float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
			new CProjectile(GameWorld(), WEAPON_SHOTGUN, m_LastDriverCID,
				m_PhysicalPos, GetDir(i*0.1f)*Speed,
				max(1, round_to_int(Server()->TickSpeed()*0.1f)),
				1, false, 1, 0.0f, -1, WEAPON_SHOTGUN);
		}
	}
	ResetToSpawn(true);
}

void CBattle::Tick()
{
	if(m_EnterCooldown > 0)
		m_EnterCooldown--;
	if(m_RepairCooldown > 0)
		m_RepairCooldown--;
	if(m_MessageCooldown > 0)
		m_MessageCooldown--;

	if(m_pDriver)
	{
		m_Pos = m_pDriver->m_Pos;
		m_PhysicalPos = m_Pos;
		m_Vel = m_pDriver->GetBattlefieldVehicleVelocity();
		m_Health = clamp(m_pDriver->GetBattlefieldVehicleHealth(), 0, MaxHealth(m_Type));
		m_LastDriverCID = m_pDriver->GetPlayer() ? m_pDriver->GetPlayer()->GetCID() : -1;
		m_JetSafelyStopped = m_pDriver->BattlefieldJetSafelyStopped();
		if(m_SnapshotTiltCooldown > 0)
			m_SnapshotTiltCooldown--;
		if(m_pDriver->IsGrounded())
		{
			if(m_SnapshotTilt > 0.0f)
				m_SnapshotTilt = max(0.0f, m_SnapshotTilt-0.1f);
			else if(m_SnapshotTilt < 0.0f)
				m_SnapshotTilt = min(0.0f, m_SnapshotTilt+0.1f);
		}
		else if(m_SnapshotTiltCooldown == 0)
		{
			int Direction = m_pDriver->GetBattlefieldInputDirection();
			if(Direction > 0)
				m_SnapshotTilt = min(0.6f, m_SnapshotTilt+0.1f);
			else if(Direction < 0)
				m_SnapshotTilt = max(-0.6f, m_SnapshotTilt-0.1f);
			else if(m_SnapshotTilt > 0.0f)
				m_SnapshotTilt = max(0.0f, m_SnapshotTilt-0.1f);
			else if(m_SnapshotTilt < 0.0f)
				m_SnapshotTilt = min(0.0f, m_SnapshotTilt+0.1f);
			m_SnapshotTiltCooldown = 3;
		}
		return;
	}

	if(m_RespawnTimer > 0)
	{
		bool CountdownExplosion = false;
		TryRepair();
		if(m_RespawnTimer > 0)
			m_RespawnTimer--;

		const int TickSpeed = Server()->TickSpeed();
		if(m_RespawnTimer == TickSpeed*3 ||
			m_RespawnTimer == TickSpeed*2 ||
			m_RespawnTimer == TickSpeed ||
			m_RespawnTimer == round_to_int(TickSpeed*0.7f) ||
			m_RespawnTimer == round_to_int(TickSpeed*0.5f) ||
			m_RespawnTimer == round_to_int(TickSpeed*0.3f) ||
			m_RespawnTimer == round_to_int(TickSpeed*0.2f) ||
			m_RespawnTimer == round_to_int(TickSpeed*0.1f))
		{
			GameServer()->CreateDamageIndd(m_Pos, -9.0f, 10);
		}

		if(m_RespawnTimer == 2)
		{
			GameServer()->CreateExplosion(m_Pos, 0, WEAPON_RIFLE, true);
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
			// Move both positions back to the spawn here but
			// deliberately leaves the countdown and velocity alive.  Tick 1
			// still simulates the loose body; tick 0 performs the full reset.
			m_Pos = m_SpawnPos;
			m_PhysicalPos = m_SpawnPos;
			m_Health = MaxHealth(m_Type);
			CountdownExplosion = true;
		}
		if(m_RespawnTimer == 0)
		{
			ResetToSpawn(true);
			TryEnter();
			return;
		}

		bool InWater = GameServer()->Collision()->isWater(
			round_to_int(m_PhysicalPos.x), round_to_int(m_PhysicalPos.y));
		if(InWater && m_Type != TYPE_UBOAT && m_Type != TYPE_SHIP && m_Type != TYPE_MINI)
		{
			GameServer()->CreateExplosion(m_PhysicalPos, 0, WEAPON_RIFLE, true);
			GameServer()->CreateSound(m_PhysicalPos, SOUND_GRENADE_EXPLODE);
			ResetToSpawn(true);
			return;
		}
		if(InWater && (m_Type == TYPE_SHIP || m_Type == TYPE_MINI))
			m_Vel.y -= 0.8f;
		if(!InWater && (m_Type == TYPE_UBOAT || m_Type == TYPE_MINI) &&
			GameServer()->Collision()->CheckPoint(m_PhysicalPos.x, m_PhysicalPos.y+10.0f))
			m_RespawnTimer = 3;
		if(GameServer()->Collision()->CheckPoint(m_PhysicalPos.x, m_PhysicalPos.y+10.0f))
			m_Vel.x = 0.0f;

		float Elasticity = 0.5f;
		if(m_Type == TYPE_JET && !m_JetSafelyStopped &&
			(GameServer()->Collision()->CheckPoint(m_PhysicalPos.x+30.0f, m_PhysicalPos.y) ||
			 GameServer()->Collision()->CheckPoint(m_PhysicalPos.x-30.0f, m_PhysicalPos.y) ||
			 GameServer()->Collision()->CheckPoint(m_PhysicalPos.x, m_PhysicalPos.y-30.0f) ||
			 GameServer()->Collision()->CheckPoint(m_PhysicalPos.x, m_PhysicalPos.y+30.0f)))
		{
			CrashAbandonedJet();
			return;
		}
		if(m_Type == TYPE_SHIP || m_Type == TYPE_MINI)
		{
			m_Vel.x *= 0.98f;
			m_Vel.y = max(-4.0f, m_Vel.y)+0.4f;
		}
		else if(m_Type == TYPE_HELI)
		{
			m_Vel.x *= 0.91f;
			m_Vel.y += 0.1f;
			if(GameServer()->Collision()->isNogo1(round_to_int(m_PhysicalPos.x), round_to_int(m_PhysicalPos.y)))
				m_Vel.y = 0.0f;
			Elasticity = 0.1f;
		}
		else if(m_Type == TYPE_UBOAT)
		{
			m_Vel.x *= 0.95f;
			m_Vel.y += 0.02f;
			if(GameServer()->Collision()->isNogo1(round_to_int(m_PhysicalPos.x), round_to_int(m_PhysicalPos.y)))
				m_Vel.y = 0.0f;
			else if(m_Vel.y > 0.3f)
				m_Vel.y = 0.2f;
			if(!InWater)
				m_Vel.y = 5.0f;
			Elasticity = 0.02f;
		}
		else
		{
			m_Vel.y += 0.5f;
			if(GameServer()->Collision()->isNogo1(round_to_int(m_PhysicalPos.x), round_to_int(m_PhysicalPos.y)))
				m_Vel.y = 0.0f;
		}
		GameServer()->Collision()->MoveBox(
			&m_PhysicalPos, &m_Vel, vec2(15, 15), Elasticity);
		if(!CountdownExplosion)
			m_Pos = m_PhysicalPos;

		// An abandoned vehicle remains immediately enterable at its simulated
		// position. The three-second entry lock belongs to the later spawn reset,
		// not to this voluntary-exit state.
		TryEnter();
		return;
	}

	m_Pos = m_SpawnPos;
	m_PhysicalPos = m_SpawnPos;
	TryEnter();
}

void CBattle::SnapLaser(int ID, vec2 From, vec2 To, int StartTickOffset)
{
	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(
		Server()->SnapNewItem(NETOBJTYPE_LASER, ID, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;
	pLaser->m_X = (int)From.x;
	pLaser->m_Y = (int)From.y;
	pLaser->m_FromX = (int)To.x;
	pLaser->m_FromY = (int)To.y;
	pLaser->m_StartTick = Server()->Tick()-StartTickOffset;
}

void CBattle::SnapProjectile(int ID, vec2 Pos, int Type, int StartTickOffset)
{
	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(
		Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, ID, sizeof(CNetObj_Projectile)));
	if(!pProj)
		return;
	pProj->m_X = (int)Pos.x;
	pProj->m_Y = (int)Pos.y;
	pProj->m_VelX = 0;
	pProj->m_VelY = 0;
	pProj->m_StartTick = Server()->Tick()-StartTickOffset;
	pProj->m_Type = Type;
}

void CBattle::SnapPickup(int ID, vec2 Pos, int Type)
{
	CNetObj_Pickup *pPickup = static_cast<CNetObj_Pickup *>(
		Server()->SnapNewItem(NETOBJTYPE_PICKUP, ID, sizeof(CNetObj_Pickup)));
	if(!pPickup)
		return;
	pPickup->m_X = (int)Pos.x;
	pPickup->m_Y = (int)Pos.y;
	pPickup->m_Type = Type;
	pPickup->m_Subtype = 0;
}

void CBattle::Snap(int SnappingClient)
{
	(void)SnappingClient;
	// Entry cooldown is interaction state, not visibility state. This matters
	// after a vehicle has returned to spawn; ordinary abandoned vehicles remain
	// visible and immediately reclaimable.

	vec2 Aim(1, 0);
	if(m_pDriver)
		Aim = m_pDriver->GetBattlefieldVehicleAim();
	else if(length(m_Vel) > 0.01f)
		Aim = normalize(m_Vel);
	float Angle = GetAngle(Aim);
	vec2 Right = GetDir(Angle);

	if(m_Type == TYPE_HELI)
	{
		float A = GetAngle(vec2(1.0f, m_pDriver ? m_SnapshotTilt : 0.0f));
		SnapPickup(m_aExtraIDs[8], m_Pos+GetDir(A)*40.0f, POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[1], m_Pos+GetDir(A)*-40.0f, POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[2], m_Pos+GetDir(A-7.9f)*80.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[3], m_Pos+GetDir(A-7.9f)*45.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[4], m_Pos+GetDir(A+7.2f)*-102.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[5], m_Pos+GetDir(A-7.2f)*102.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[6], m_Pos+GetDir(A-7.5f)*85.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[7], m_Pos+GetDir(A+7.5f)*-85.0f, POWERUP_ARMOR);
	}
	else if(m_Type == TYPE_JET)
	{
		SnapPickup(m_aExtraIDs[8], m_Pos+Right*100.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[1], m_Pos+Right*70.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[2], m_Pos+Right*-70.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[3], m_Pos+Right*-30.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[4], m_Pos+GetDir(Angle+13.0f)*50.0f, POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[5], m_Pos+GetDir(Angle-13.0f)*50.0f, POWERUP_HEALTH);
	}
	else if(m_Type == TYPE_PANZER)
	{
		SnapPickup(m_aExtraIDs[8], m_Pos+vec2(50, 0), POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[1], m_Pos+vec2(-50, 0), POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[2], m_Pos+vec2(-15, -45), POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[3], m_Pos+vec2(15, -45), POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[4], m_Pos+vec2(-35, -25), POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[5], m_Pos+vec2(35, -25), POWERUP_ARMOR);
	}
	else if(m_Type == TYPE_CAR)
	{
		SnapPickup(m_aExtraIDs[8], m_Pos+vec2(35, 0), POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[1], m_Pos+vec2(-35, 0), POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[2], m_Pos+vec2(0, -40), POWERUP_ARMOR);
	}
	else if(m_Type == TYPE_UBOAT)
	{
		SnapPickup(m_aExtraIDs[8], m_Pos+vec2(45, 0), POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[1], m_Pos+vec2(-45, 0), POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[2], m_Pos+vec2(-20, -30), POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[3], m_Pos+vec2(20, -30), POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[4], m_Pos+vec2(20, 30), POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[5], m_Pos+vec2(-20, 30), POWERUP_ARMOR);
	}
	else if(m_Type == TYPE_SHIP)
	{
		float A = GetAngle(vec2(1.0f, m_pDriver ? -m_SnapshotTilt : 0.0f));
		vec2 Center = m_Pos+vec2(0, 20);
		SnapPickup(m_aExtraIDs[8], Center+GetDir(A)*20.0f, POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[1], Center+GetDir(A)*-20.0f, POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[2], Center+GetDir(A)*55.0f, POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[3], Center+GetDir(A)*-55.0f, POWERUP_HEALTH);
		SnapPickup(m_aExtraIDs[4], Center+GetDir(A+7.2f)*-35.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[5], Center+GetDir(A-7.2f)*35.0f, POWERUP_ARMOR);
	}
	else if(m_Type == TYPE_MINI)
	{
		SnapPickup(m_aExtraIDs[8], m_Pos+Right*30.0f, POWERUP_ARMOR);
		SnapPickup(m_aExtraIDs[4], m_Pos+Right*-30.0f, POWERUP_ARMOR);
		SnapProjectile(m_aExtraIDs[1], m_Pos+Right*60.0f, WEAPON_SHOTGUN, 1);
		SnapProjectile(m_aExtraIDs[2], m_Pos+GetDir(Angle+13.0f)*50.0f, WEAPON_SHOTGUN, 1);
		SnapProjectile(m_aExtraIDs[3], m_Pos+GetDir(Angle-13.0f)*50.0f, WEAPON_SHOTGUN, 1);
	}

	if(m_Repairing)
		SnapLaser(m_aExtraIDs[0], m_Pos, m_RepairerPos);
}
