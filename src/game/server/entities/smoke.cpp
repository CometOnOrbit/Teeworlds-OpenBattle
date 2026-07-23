#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "smoke.h"

static vec2 SmokeNormalize(vec2 Direction)
{
	return length(Direction) > 0.0001f ? normalize(Direction) : vec2(1, 0);
}

CSmoke::CSmoke(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner, int Type)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_ProximityRadius = 25.0f;
	m_Pos = Pos;
	m_BasePos = Pos;
	m_BaseVel = vec2(0, 0);
	m_ProjectilePos1 = Pos+vec2(0, 10);
	m_ProjectilePos2 = Pos-vec2(0, 10);
	m_Direction = Direction;
	m_Vel = Direction;
	m_Owner = Owner;
	m_Type = Type;
	m_LifeTimer = Server()->TickSpeed()*3;
	m_UserCID = -1;
	m_FiringTimer = 0;
	m_FiringDelay = 0;
	m_BurstTimer = 0;
	m_OscillationTimer = 40;
	m_OscillationPositive = Direction.x > 0.0f;
	// Reserve all five auxiliary IDs for both launcher and cloud
	// instances, even though cloud snapshots do not emit them.
	AllocExtraIDs(5);
	GameWorld()->InsertEntity(this);
}

void CSmoke::Reset()
{
	// Intentionally empty. Smoke entities own their lifetime in Tick and
	// survive a game-world reset.
}

void CSmoke::TickCloud()
{
	if(m_LifeTimer > 0)
		m_LifeTimer--;
	if(m_LifeTimer == 0)
		GameServer()->m_World.DestroyEntity(this);

	if(m_OscillationTimer > 0)
		m_OscillationTimer--;
	else if(m_OscillationTimer < 0)
		m_OscillationTimer++;

	if(!GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y-10.0f))
	{
		if(GameServer()->Collision()->CheckPoint(m_Pos.x-40.0f, m_Pos.y))
			m_Pos.x += 8.0f;
		else if(GameServer()->Collision()->CheckPoint(m_Pos.x+40.0f, m_Pos.y))
			m_Pos.x -= 8.0f;
		else
		{
			float Speed = GameServer()->Collision()->isWater(round_to_int(m_Pos.x), round_to_int(m_Pos.y)) ?
				0.5f : 5.0f;
			m_Pos += SmokeNormalize(m_Direction)*Speed;
		}
	}

	std::list<CCharacter *> Characters = GameWorld()->IntersectedCharacters(
		m_Pos, m_Pos+vec2(1, 1), 300.0f, 0, true);
	for(std::list<CCharacter *>::iterator It = Characters.begin(); It != Characters.end(); ++It)
	{
		CCharacter *pCharacter = *It;
		if(pCharacter && pCharacter->IsAlive() &&
			!GameServer()->Collision()->IntersectLine(m_Pos, pCharacter->m_Pos, 0, 0))
			pCharacter->ApplyBattlefieldSmokeEmote();
	}
	GameServer()->CreateDeath(m_Pos, -1);

	if(m_OscillationPositive)
		m_Direction.x += m_OscillationTimer > 0 ? -7.0f : 7.0f;
	else
		m_Direction.x += m_OscillationTimer > 0 ? 7.0f : -7.0f;
	if(m_OscillationTimer == 1)
		m_OscillationTimer = -40;
	else if(m_OscillationTimer == -1)
		m_OscillationTimer = 40;
}

void CSmoke::SpawnCloudBurst()
{
	static const vec2 s_aDirections[] = {
		vec2(130, -100), vec2(250, -200), vec2(100, -100), vec2(200, -300),
		vec2(100, -200), vec2(-130, -100), vec2(-100, -200), vec2(-150, -300),
		vec2(-100, -100), vec2(-200, -200)};
	int Step = m_BurstTimer == 0 ? 0 :
		m_BurstTimer/max(1, Server()->TickSpeed()/2);
	Step = clamp(Step, 0, (int)(sizeof(s_aDirections)/sizeof(s_aDirections[0]))-1);
	new CSmoke(GameWorld(), m_Pos, s_aDirections[Step], m_Owner, TYPE_CLOUD);
}

void CSmoke::TickLauncher()
{
	if(m_FiringTimer > 0)
		m_FiringTimer--;
	if(m_FiringDelay > 0)
		m_FiringDelay--;
	if(m_BurstTimer > 0)
		m_BurstTimer--;

	// The map anchor and the temporarily launched position are two independent
	// bodies. The tall anchor supplies the three decorative
	// lasers; m_Pos supplies the two rotating shotgun projectiles.
	m_BaseVel.y = 0.5f;
	GameServer()->Collision()->MoveBox(&m_BasePos, &m_BaseVel, vec2(10, 60), 0.5f);

	if(m_UserCID < 0)
	{
		if(GameServer()->Collision()->isWater(round_to_int(m_Pos.x), round_to_int(m_Pos.y)))
		{
			if(m_Vel.x > 1.0f)
				m_Vel.x -= 1.0f;
			else if(m_Vel.x < -1.0f)
				m_Vel.x += 1.0f;
			m_Vel.y += 0.1f;
		}
		else
			m_Vel.y += 0.5f;

		if(GameServer()->IntersectBattlefieldDoor(m_Pos, m_Pos+vec2(1, 1)))
			m_Vel.x = 0.0f;
		// CSmoke::Tick passes 1.0 elasticity for the unclaimed launcher.
		GameServer()->Collision()->MoveBox(&m_Pos, &m_Vel, vec2(9, 9), 1.0f);
	}

	CCharacter *pUser = m_UserCID >= 0 ? GameServer()->GetPlayerChar(m_UserCID) : 0;
	CCharacter *pClosest = m_FiringTimer == 0 ?
		GameWorld()->ClosestCharacter(m_Pos, 20.0f, 0) : 0;
	if(pClosest && pClosest->IsAlive() && pClosest->HasSmokeLauncher())
	{
		// Return from Tick here. This gives an occupied launcher
		// its characteristic roughly 40-pixel follow steps instead of welding
		// its snapshot to the user's position every tick.
		return;
	}
	if(!pUser || !pUser->IsAlive())
	{
		if(pUser)
			pUser->ReleaseSmokeLauncher();
		m_UserCID = -1;
		pUser = pClosest;
		if(pUser && pUser->IsAlive() && pUser->GetPlayer() && pUser->ClaimSmokeLauncher())
		{
			m_UserCID = pUser->GetPlayer()->GetCID();
			GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
		}
	}

	if(pUser && pUser->IsAlive() && m_FiringTimer == 0)
	{
		m_Pos = pUser->m_Pos;
		if(pUser->WantsSmokeLauncherFire())
		{
			m_Direction = pUser->GetSmokeLauncherAim();
			m_Vel = m_Direction*10.0f;
			pUser->ReleaseSmokeLauncher();
			m_UserCID = -1;
			m_FiringTimer = Server()->TickSpeed()*18;
			m_FiringDelay = Server()->TickSpeed()*3;
			m_BurstTimer = 0;
		}
	}
	else if(m_FiringTimer == 0)
		m_Pos = m_BasePos;
	else if(m_FiringTimer == 2)
		m_Pos = m_BasePos;

	if(m_FiringTimer > 0 && m_FiringDelay == 0)
	{
		// Emit this sound every active smoke tick and restart the
		// ten-cloud sequence whenever its five-second burst timer reaches zero.
		// The 18-second flight with a three-second delay therefore produces
		// three complete bursts, not a single burst followed by an idle timer.
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		if(m_BurstTimer == 0)
		{
			SpawnCloudBurst();
			m_BurstTimer = Server()->TickSpeed()*5;
		}
		else if(m_BurstTimer > 0 && m_BurstTimer < Server()->TickSpeed()*5 &&
			m_BurstTimer%max(1, Server()->TickSpeed()/2) == 0)
			SpawnCloudBurst();
	}

	const float Phase = Server()->Tick()*0.1f;
	m_ProjectilePos1 = m_Pos+GetDir(Phase)*-10.0f;
	m_ProjectilePos2 = m_Pos+GetDir(Phase)*10.0f;
}

void CSmoke::Tick()
{
	if(m_Type == TYPE_CLOUD)
		TickCloud();
	else if(m_Type == TYPE_LAUNCHER)
		TickLauncher();
}

void CSmoke::SnapProjectile(int ID, vec2 Pos)
{
	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(
		Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, ID, sizeof(CNetObj_Projectile)));
	if(!pProj)
		return;
	pProj->m_X = (int)Pos.x;
	pProj->m_Y = (int)Pos.y;
	pProj->m_VelX = pProj->m_VelY = 0;
	pProj->m_Type = WEAPON_SHOTGUN;
	pProj->m_StartTick = Server()->Tick()-1;
}

void CSmoke::SnapLaser(int ID, vec2 From, vec2 To)
{
	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(
		Server()->SnapNewItem(NETOBJTYPE_LASER, ID, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;
	pLaser->m_X = (int)From.x;
	pLaser->m_Y = (int)From.y;
	pLaser->m_FromX = (int)To.x;
	pLaser->m_FromY = (int)To.y;
	pLaser->m_StartTick = Server()->Tick()-3;
}

void CSmoke::Snap(int SnappingClient)
{
	(void)SnappingClient;
	if(m_Type != TYPE_LAUNCHER)
		return;
	if(m_UserCID < 0)
	{
		SnapProjectile(m_aExtraIDs[0], m_ProjectilePos1);
		SnapProjectile(m_aExtraIDs[1], m_ProjectilePos2);
	}
	SnapLaser(m_aExtraIDs[3], m_BasePos+vec2(-15, 15), m_BasePos+vec2(15, 15));
	SnapLaser(m_aExtraIDs[4], m_BasePos+vec2(15, 15), m_BasePos+vec2(45, 45));
	SnapLaser(m_aExtraIDs[2], m_BasePos+vec2(-15, 15), m_BasePos+vec2(-45, 45));
}
