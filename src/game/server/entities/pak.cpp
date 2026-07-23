#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "pak.h"

static vec2 PakNormalize(vec2 Direction)
{
	return length(Direction) > 0.0001f ? normalize(Direction) : vec2(1, 0);
}

CPak::CPak(CGameWorld *pGameWorld, int Owner, int Mode, vec2 Pos, int TargetCID)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_Owner = Owner;
	m_Mode = Mode;
	m_TargetCID = TargetCID;
	m_Pos = Pos;
	m_LifeTimer = Server()->TickSpeed()*20;
	m_AccelerationTimer = round_to_int(Server()->TickSpeed()*0.9f);
	m_LockTimer = 0;
	m_SignalLost = false;
	m_SampleToggle = 0;
	m_SamplePos = vec2(0, 0);
	m_Direction = vec2(0, 0);
	m_Vel = vec2(0, 0);
	m_pLockTarget = 0;
	// Reserve four IDs in addition to CEntity::m_ID. The base ID
	// is deliberately unused by every CPak snapshot layout.
	AllocExtraIDs(4);
	GameWorld()->InsertEntity(this);
}

CPak::~CPak()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(pOwner)
		pOwner->OnPakDestroyed(this, m_Mode);
}

void CPak::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CPak::ExplodeSingle()
{
	GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_GRENADE, false);
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	Reset();
}

void CPak::ExplodeRemoteAxis(bool HorizontalCollision)
{
	// A hit on an X-side wall spreads vertically; a Y-side hit spreads
	// horizontally. Both blocks may execute in one tick at a corner.
	const vec2 Axis = HorizontalCollision ? vec2(0, 50) : vec2(50, 0);
	GameServer()->CreateExplosion(m_Pos+Axis, m_Owner, WEAPON_GRENADE, false);
	GameServer()->CreateSound(m_Pos+Axis, SOUND_GRENADE_EXPLODE);
	GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_GRENADE, false);
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameServer()->CreateExplosion(m_Pos-Axis, m_Owner, WEAPON_GRENADE, false);
	GameServer()->CreateSound(m_Pos-Axis, SOUND_GRENADE_EXPLODE);
	Reset();
}

void CPak::TickTargeter(CCharacter *pOwner)
{
	if(!pOwner)
	{
		Reset();
		return;
	}

	vec2 Cursor = pOwner->GetPlayer() && pOwner->GetPlayer()->BattlefieldAimCameraActive() ?
		pOwner->GetPlayer()->m_ViewPos : pOwner->GetAntiTankCursor();
	if(distance(pOwner->m_Pos, Cursor) > 1300.0f)
	{
		GameServer()->SendBroadcast(
			"To far away from the station! Go back with your cursor!", m_Owner);
		pOwner->SetBattlefieldBroadcastTimer(3);
		m_SignalLost = true;
		return;
	}

	m_SignalLost = false;
	m_Pos = Cursor;
	CCharacter *pCandidate = GameWorld()->ClosestCharacter(m_Pos, 50.0f, 0);

	// These are queued-destruction tests in CPak::Tick. The lock pass still
	// completes in the same tick after any one of them marks the entity.
	if(!pOwner->InAntiTankCannon())
		Reset();
	if(pOwner->GetAntiTankFireCooldown() > 0)
		Reset();
	if(pOwner->GetAntiTankMode() != 1)
		Reset();

	if(!m_pLockTarget || !m_pLockTarget->IsAlive())
	{
		if(pCandidate && pCandidate->IsAlive() && pCandidate->GetPlayer() &&
			pOwner->GetPlayer() &&
			pCandidate->GetPlayer()->GetTeam() != pOwner->GetPlayer()->GetTeam())
		{
			m_pLockTarget = pCandidate;
			m_LockTimer = Server()->TickSpeed()*3;
		}
	}
	else
	{
		if(m_LockTimer > 1)
			m_LockTimer--;
		if(m_LockTimer == 1)
			pOwner->SetAntiTankTarget(m_pLockTarget->GetPlayer()->GetCID());

		if(m_LockTimer == 140 || m_LockTimer == 110 || m_LockTimer == 85 ||
			m_LockTimer == 65 || m_LockTimer == 50 || m_LockTimer == 40 ||
			m_LockTimer == 30 || m_LockTimer == 22 || m_LockTimer == 15 ||
			m_LockTimer == 10 || m_LockTimer == 5 || m_LockTimer == 1)
			GameServer()->CreateSound(pOwner->m_Pos, SOUND_WEAPON_NOAMMO);

		m_Pos = m_pLockTarget->m_Pos;
		if(distance(m_Pos, Cursor) > 300.0f)
			m_pLockTarget = 0;
	}
}

void CPak::TickGuidedMissile(CCharacter *pOwner)
{
	CCharacter *pTarget = GameServer()->GetPlayerChar(m_TargetCID);
	CCharacter *pHit = GameWorld()->ClosestCharacter(m_Pos, 20.0f, pOwner);

	if(pTarget && pTarget->IsAlive())
	{
		m_Direction = PakNormalize(pTarget->m_Pos-m_Pos);
		if(m_AccelerationTimer == 0)
		{
			m_Vel.x = m_Direction.x*30.0f;
			m_Vel.y += m_Direction.y;
		}
		else
		{
			m_AccelerationTimer--;
			m_Vel.x = m_Direction.x*2.0f;
			m_Vel.y -= 0.5f;
		}
	}

	GameServer()->Collision()->MoveBox(&m_Pos, &m_Vel, vec2(1, 1), 0.0f);
	bool Solid =
		GameServer()->Collision()->CheckPoint(m_Pos.x+10.0f, m_Pos.y) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x-10.0f, m_Pos.y) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y-10.0f) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y+10.0f);

	if(pHit && pHit->IsAlive())
	{
		int Damage = pHit->InBattlefieldVehicle() ?
			pHit->GetBattlefieldVehicleHealth() : 10;
		pHit->TakeDamage(vec2(0, 0), Damage, m_Owner, WEAPON_GRENADE);
		ExplodeSingle();
		return;
	}
	if(Solid)
		ExplodeSingle();
}

void CPak::TickRemoteGrenade(CCharacter *pOwner)
{
	CCharacter *pHit = GameWorld()->ClosestCharacter(m_Pos, 6.0f, pOwner);
	if(--m_LifeTimer == 0)
		ExplodeSingle();

	if(!pOwner)
	{
		Reset();
		return;
	}

	if(!pOwner->InAntiTankCannon() || pOwner->GetAntiTankMode() != 2)
	{
		m_SignalLost = true;
		pOwner->ReleaseRemoteGrenadeControl(this);
	}

	if(distance(pOwner->m_Pos, m_Pos) > 1300.0f && !m_SignalLost)
	{
		pOwner->ReleaseRemoteGrenadeControl(this);
		m_SignalLost = true;
		GameServer()->SendBroadcast("Grenade lost signale!", m_Owner);
		pOwner->SetBattlefieldBroadcastTimer(100);
	}

	if(!m_SignalLost)
	{
		vec2 Cursor = pOwner->GetPlayer() && pOwner->GetPlayer()->BattlefieldAimCameraActive() ?
			pOwner->GetPlayer()->m_ViewPos : pOwner->GetAntiTankCursor();
		vec2 Direction = PakNormalize(Cursor-m_Pos);
		m_Vel += Direction*2.0f;
		if(distance(m_Pos, m_SamplePos) > 20.0f)
			m_Vel *= 0.8f;
	}

	GameServer()->Collision()->MoveBox(&m_Pos, &m_Vel, vec2(1, 1), 0.0f);
	if(m_SampleToggle == 0)
	{
		m_SamplePos = m_Pos;
		m_SampleToggle = 1;
	}
	else if(m_SampleToggle == 1)
		m_SampleToggle = 0;

	bool HorizontalCollision =
		GameServer()->Collision()->CheckPoint(m_Pos.x+10.0f, m_Pos.y) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x-10.0f, m_Pos.y);
	if(HorizontalCollision)
	{
		pOwner->ReleaseRemoteGrenadeControl(this);
		ExplodeRemoteAxis(true);
	}

	bool VerticalCollision =
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y-10.0f) ||
		GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y+10.0f);
	if(VerticalCollision)
	{
		pOwner->ReleaseRemoteGrenadeControl(this);
		ExplodeRemoteAxis(false);
	}

	if(pHit && pHit->IsAlive())
	{
		pOwner->ReleaseRemoteGrenadeControl(this);
		pHit->TakeDamage(vec2(0, 0), pHit->InBattlefieldVehicle() ? 9 : 6,
			m_Owner, WEAPON_GRENADE);
		ExplodeSingle();
	}
}

void CPak::Tick()
{
	CPlayer *pOwnerPlayer = m_Owner >= 0 && m_Owner < MAX_CLIENTS ?
		GameServer()->m_apPlayers[m_Owner] : 0;
	if(!pOwnerPlayer)
	{
		Reset();
		return;
	}

	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(m_Mode == MODE_TARGETER)
		TickTargeter(pOwner);
	if(m_Mode == MODE_GUIDED_MISSILE)
		TickGuidedMissile(pOwner);
	if(m_Mode == MODE_REMOTE_GRENADE)
		TickRemoteGrenade(pOwner);
}

void CPak::SnapProjectile(int ID, vec2 Pos, vec2 Vel, int Type)
{
	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(
		Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, ID, sizeof(CNetObj_Projectile)));
	if(!pProj)
		return;
	pProj->m_X = round_to_int(Pos.x);
	pProj->m_Y = round_to_int(Pos.y);
	pProj->m_VelX = round_to_int(Vel.x);
	pProj->m_VelY = round_to_int(Vel.y);
	pProj->m_Type = Type;
	pProj->m_StartTick = Server()->Tick()-1;
}

void CPak::Snap(int SnappingClient)
{
	(void)SnappingClient;

	if(m_Mode == MODE_TARGETER)
	{
		if(!m_pLockTarget || !m_pLockTarget->IsAlive() || m_SignalLost)
			return;
		float Radius = 40.0f+(float)m_LockTimer;
		SnapProjectile(m_aExtraIDs[0], m_Pos+vec2(Radius, 0), vec2(10, -1), WEAPON_GUN);
		SnapProjectile(m_aExtraIDs[1], m_Pos+vec2(0, -Radius), vec2(0, -10), WEAPON_GUN);
		SnapProjectile(m_aExtraIDs[2], m_Pos+vec2(0, Radius), vec2(0, 10), WEAPON_GUN);
		SnapProjectile(m_aExtraIDs[3], m_Pos+vec2(-Radius, 0), vec2(-10, -1), WEAPON_GUN);
		return;
	}

	if(m_Mode == MODE_GUIDED_MISSILE || m_Mode == MODE_REMOTE_GRENADE)
	{
		vec2 Direction = PakNormalize(m_Vel);
		SnapProjectile(m_aExtraIDs[3], m_Pos, vec2(0, 0), WEAPON_GRENADE);
		SnapProjectile(m_aExtraIDs[0], m_Pos+Direction*20.0f, vec2(0, 0), WEAPON_SHOTGUN);
		SnapProjectile(m_aExtraIDs[1], m_Pos-Direction*20.0f, vec2(0, 0), WEAPON_SHOTGUN);
	}
}
