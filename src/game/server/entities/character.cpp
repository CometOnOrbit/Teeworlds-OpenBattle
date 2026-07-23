/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/sixup_snap.h>
#include <game/mapitems.h>

#include "character.h"
#include "ammo.h"
#include "battle.h"
#include "bomb.h"
#include "pak.h"
#include "c4.h"
#include "health_shot.h"
#include "laser.h"
#include "mine.h"
#include "projectile.h"
#include "guided_projectile.h"
#include "shield.h"

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}

static vec2 SafeNormalize(vec2 V, vec2 Fallback = vec2(1.0f, 0.0f))
{
	float Len = length(V);
	if(Len <= 0.001f)
		return Fallback;
	return vec2(V.x/Len, V.y/Len);
}


MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
	m_BattlefieldTileCooldown = 0;
	m_BroadcastClearTimer = 0;
	m_EngineerAction = false;
	m_EngineerToolCooldown = 0;
	m_NumMines = 0;
	m_NumC4 = 0;
	m_MinesSinceSupply = 0;
	m_C4SinceSupply = 0;
	m_NumHealthKits = 0;
	m_NumAmmoPacks = 0;
	m_DeployCooldown = 0;
	m_SelfAmmoCooldown = 0;
	m_NinjaSwitchCooldown = 0;
	m_NinjaMovementBlocked = false;
	m_ClassGunMagazine = 0;
	m_SoldierGunMagazine = 0;
	m_HarpoonAmmo = 0;
	m_ArrowAmmo = 0;
	m_ThrowingStarAmmo = 0;
	m_EngineerGrenadeMode = false;
	m_InBattlefieldWater = false;
	m_BattlefieldOxygenActive = false;
	m_BattlefieldOxygenCooldown = 0;
	m_WaterWeaponCooldown = 0;
	m_HarpoonCharge = 0;
	m_HarpoonCharging = false;
	m_pBattlefieldVehicle = 0;
	m_pBattlefieldSeatDriver = 0;
	m_pBattlefieldPassenger = 0;
	m_BattlefieldVehiclePassenger = false;
	m_BattlefieldVehicleType = BATTLEFIELD_VEHICLE_NONE;
	m_BattlefieldVehicleHealth = 0;
	m_BattlefieldVehicleWeaponCooldown = 0;
	m_BattlefieldVehicleAltCooldown = 0;
	m_BattlefieldVehicleFireToggle = false;
	m_BattlefieldHeliBombActive = false;
	m_BattlefieldVehicleEntryCooldown = 0;
	m_BattlefieldRepairCooldown = 0;
	m_EngineerObstructedCooldown = 0;
	m_OccupiedVehicleAmmoRegenCooldown = 0;
	m_BattlefieldVehicleSpeed = 0;
	m_BattlefieldVehicleAmmo = 10;
	m_BattlefieldVehicleBurstShots = 0;
	m_BattlefieldVehicleAmmoRegen = 0;
	m_BattlefieldVehicleOverheat = 0;
	m_BattlefieldVehicleMoveSound = 0;
	m_BattlefieldVehicleGrace = 0;
	m_HeliCollisionGrace = 0;
	m_HeliPassengerEntryDelay = 0;
	m_HeliBombCooldown = 0;
	m_JetFeedbackCooldown = 0;
	m_BattlefieldVehicleLandingCounter = 0;
	m_BattlefieldVehicleAirborne = false;
	m_BattlefieldVehicleSafeLanding = false;
	m_BattlefieldVehicleLanding = false;
	m_BattlefieldVehicleExitRequested = false;
	m_BattlefieldVehicleWaterResetPending = false;
	m_BattlefieldVehicleScoreboardHeld = false;
	m_BattlefieldUboatOutsideWater = false;
	m_BattlefieldUboatCollisionLatched = false;
	m_InAntiTankCannon = false;
	m_AntiTankStationLatched = false;
	m_InAntiAircraftCannon = false;
	m_AntiAircraftStationLatched = false;
	m_AntiAircraftStationPos = vec2(0, 0);
	m_AntiAircraftBurstCount = 0;
	m_AntiTankStationPos = vec2(0, 0);
	m_AntiTankMode = 1;
	m_AntiTankTargetCID = -1;
	m_AntiTankFireCooldown = 0;
	m_pAntiTankTargeter = 0;
	m_pAntiTankRemoteGrenade = 0;
	m_KillStreak = 0;
	m_GodModeTimer = 0;
	m_HandGrenadeAvailable = true;
	m_AirstrikeBurstTimer = 0;
	m_AirstrikeCooldown = 0;
	m_AirstrikeShotCooldown = 0;
	m_Invisible = false;
	m_InvisibilityPower = 0;
	m_MedicRegenCooldown = 0;
	m_MedicDamageCooldown = 0;
	m_MedicHealCooldown = 0;
	m_MedicHealSecondCooldown = 0;
	m_HasSmokeLauncher = false;
	m_SmokeLauncherFireRequested = false;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	// Spawn allocates three character-owned snapshot IDs. They
	// are reserved even while the player is infantry and keep all subsequent
	// dynamic entity IDs (projectiles, deployables, vehicle geometry) aligned.
	AllocExtraIDs(3);

	m_EmoteStop = -1;
	m_LastAction = -1;
	// Every spawn starts on the hammer. Class loadouts only change
	// ownership/ammo here; weapon switching remains input driven.
	m_ActiveWeapon = WEAPON_HAMMER;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;

	m_pPlayer = pPlayer;
	m_Pos = Pos;
	m_BattlefieldTileLastPos = Pos;
	m_BroadcastClearTimer = 0;
	m_EngineerAction = false;
	m_EngineerToolCooldown = 0;
	m_NinjaSwitchCooldown = 0;
	m_NinjaMovementBlocked = false;
	m_ClassGunMagazine = 0;
	m_SoldierGunMagazine = 0;
	m_InBattlefieldWater = false;
	m_BattlefieldOxygenActive = false;
	m_BattlefieldOxygenCooldown = 0;
	m_HarpoonCharging = false;
	m_pBattlefieldVehicle = 0;
	m_pBattlefieldSeatDriver = 0;
	m_pBattlefieldPassenger = 0;
	m_BattlefieldVehiclePassenger = false;
	m_BattlefieldVehicleType = BATTLEFIELD_VEHICLE_NONE;
	m_BattlefieldVehicleHealth = 0;
	m_BattlefieldVehicleWeaponCooldown = 0;
	m_BattlefieldVehicleAltCooldown = 0;
	m_BattlefieldVehicleFireToggle = false;
	m_BattlefieldHeliBombActive = false;
	m_BattlefieldVehicleEntryCooldown = 0;
	m_BattlefieldRepairCooldown = 0;
	m_EngineerObstructedCooldown = 0;
	m_OccupiedVehicleAmmoRegenCooldown = 0;
	m_BattlefieldVehicleSpeed = 0;
	m_BattlefieldVehicleAmmo = 10;
	m_BattlefieldVehicleBurstShots = 0;
	m_BattlefieldVehicleAmmoRegen = 0;
	m_BattlefieldVehicleOverheat = 0;
	m_BattlefieldVehicleMoveSound = 0;
	m_BattlefieldVehicleGrace = 0;
	m_HeliCollisionGrace = 0;
	m_HeliPassengerEntryDelay = 0;
	m_HeliBombCooldown = 0;
	m_JetFeedbackCooldown = 0;
	m_BattlefieldVehicleLandingCounter = 0;
	m_BattlefieldVehicleAirborne = false;
	m_BattlefieldVehicleSafeLanding = false;
	m_BattlefieldVehicleLanding = false;
	m_BattlefieldVehicleExitRequested = false;
	m_BattlefieldVehicleWaterResetPending = false;
	m_BattlefieldVehicleScoreboardHeld = false;
	m_BattlefieldUboatOutsideWater = false;
	m_BattlefieldUboatCollisionLatched = false;
	m_InAntiTankCannon = false;
	m_AntiTankStationLatched = false;
	m_InAntiAircraftCannon = false;
	m_AntiAircraftStationLatched = false;
	m_AntiAircraftStationPos = vec2(0, 0);
	m_AntiAircraftBurstCount = 0;
	m_AntiTankStationPos = vec2(0, 0);
	m_AntiTankMode = 1;
	m_AntiTankTargetCID = -1;
	m_AntiTankFireCooldown = 0;
	m_pAntiTankTargeter = 0;
	m_pAntiTankRemoteGrenade = 0;
	m_KillStreak = 0;
	m_GodModeTimer = 0;
	m_HandGrenadeAvailable = true;
	m_AirstrikeBurstTimer = 0;
	m_AirstrikeCooldown = 0;
	m_AirstrikeShotCooldown = 0;
	m_Invisible = false;
	m_InvisibilityPower = 0;
	m_MedicRegenCooldown = 0;
	m_MedicDamageCooldown = 0;
	m_MedicHealCooldown = 0;
	m_MedicHealSecondCooldown = 0;
	m_HasSmokeLauncher = false;
	m_SmokeLauncherFireRequested = false;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);
	ApplyBattlefieldLoadout();

	return true;
}

void CCharacter::Destroy()
{
	LeaveAntiTankCannon();
	LeaveAntiAircraftCannon();
	LeaveBattlefieldVehicle(false, true);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}


void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		m_ActiveWeapon = WEAPON_RIFLE;
		m_NinjaMovementBlocked = false;
		m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0 && !m_NinjaMovementBlocked)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

				if(aEnts[i]->IsAlive())
				{
					aEnts[i]->TakeDamage(vec2(0, 10.0f),
						aEnts[i]->InBattlefieldVehicle() ? 1 : 10,
						m_pPlayer->GetCID(), WEAPON_NINJA);
				}
			}
		}

		return;
	}

	return;
}


void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	// Battlefield replaces the vanilla "Ninja owned" lock with a 150-tick
	// lock on switching back to Ninja after its attack. Switching away remains
	// possible while the power-up is owned.
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 ||
		(m_QueuedWeapon == WEAPON_NINJA && m_NinjaSwitchCooldown > 0))
		return;
	if((InBattlefieldVehicle() &&
		m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_CAR) ||
		m_InAntiTankCannon || m_InAntiAircraftCannon)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if(m_ReloadTimer != 0)
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	if(!m_InBattlefieldWater || InBattlefieldVehicle())
	{
		m_HarpoonCharge = 0;
		m_HarpoonCharging = false;
	}
	else
	{
		if(m_Input.m_Jump && m_pPlayer->HasHarpoon() && m_HarpoonAmmo > 0)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Force: %i ; Ammo: %i", m_pPlayer->GetCID()),
				m_HarpoonCharge/20, m_HarpoonAmmo);
			GameServer()->SendBroadcast(aBuf, m_pPlayer->GetCID());
			m_BroadcastClearTimer = 50;
			if(!m_HarpoonCharging)
			{
				m_HarpoonCharging = true;
				return;
			}
			if(m_HarpoonCharge < 200)
				m_HarpoonCharge++;
			return;
		}

		if(m_HarpoonCharge != 0)
		{
			new CLaser(GameWorld(), m_Pos+vec2(0, -25.0f), Direction,
				(float)(m_HarpoonCharge/20), m_pPlayer->GetCID(), 3);
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_AIRJUMP);
			m_HarpoonAmmo--;
		}
		m_HarpoonCharging = false;
		m_HarpoonCharge = 0;

		if(m_Input.m_Jump && m_pPlayer->HasArrows() && m_ArrowAmmo > 0 &&
			m_WaterWeaponCooldown == 0)
		{
			new CLaser(GameWorld(), m_Pos, Direction, 0.0f,
				m_pPlayer->GetCID(), 4);
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_AIRJUMP);
			m_ArrowAmmo--;
			m_WaterWeaponCooldown = round_to_int(Server()->TickSpeed()*0.4f);
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Ammo: %i", m_pPlayer->GetCID()), m_ArrowAmmo);
			GameServer()->SendBroadcast(aBuf, m_pPlayer->GetCID());
			m_BroadcastClearTimer = 50;
		}
		else if(m_Input.m_Jump && m_pPlayer->HasThrowingStar() &&
			m_ThrowingStarAmmo > 0 && m_WaterWeaponCooldown == 0)
		{
			new CLaser(GameWorld(), m_Pos, Direction, 0.0f,
				m_pPlayer->GetCID(), 5);
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_AIRJUMP);
			m_ThrowingStarAmmo--;
			m_WaterWeaponCooldown = round_to_int(Server()->TickSpeed()*0.8f);
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Ammo: %i", m_pPlayer->GetCID()), m_ThrowingStarAmmo);
			GameServer()->SendBroadcast(aBuf, m_pPlayer->GetCID());
			m_BroadcastClearTimer = 50;
		}

		if(m_WaterWeaponCooldown > 0)
			m_WaterWeaponCooldown--;
	}

	bool FullAuto = false;
	if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
		FullAuto = true;
	if(m_ActiveWeapon == WEAPON_GUN &&
		(m_pPlayer->IsSoldier() || m_pPlayer->IsMedic() || m_pPlayer->IsEngineer() ||
		 m_InAntiAircraftCannon ||
		 (m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI &&
		  (!m_pBattlefieldPassenger || m_BattlefieldVehiclePassenger)) ||
		 m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_SHIP ||
		 (m_BattlefieldVehiclePassenger &&
		  m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER)))
		FullAuto = true;


	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	if(m_InAntiAircraftCannon)
	{
		float BaseAngle = GetAngle(Direction);
		for(int i = -1; i <= 1; i += 2)
		{
			CProjectile *pProjectile = new CProjectile(GameWorld(), WEAPON_GUN,
				m_pPlayer->GetCID(), m_Pos, GetDir(BaseAngle+i*0.2f),
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
				1, false, 4, 0.0f, -1, WEAPON_GUN);
			SendBattlefieldExtraProjectile(pProjectile);
		}
		GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		if(m_AntiAircraftBurstCount == 3)
		{
			m_AntiAircraftBurstCount = 0;
			m_ReloadTimer = 40;
		}
		else
		{
			m_AntiAircraftBurstCount++;
			m_ReloadTimer = 7;
		}
		return;
	}

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		return;
	}
	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_JET)
		return;

	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;

	switch(m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
			// reset objects Hit
			m_NumObjectsHit = 0;
			GameServer()->CreateSound(m_Pos,
				m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_MINI ?
				SOUND_SHOTGUN_FIRE : SOUND_HAMMER_FIRE);

			CCharacter *apEnts[MAX_CLIENTS];
			int Hits = 0;
			int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];

				if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
					continue;

				if(pTarget->GetPlayer() &&
					pTarget->GetPlayer()->GetTeam() == m_pPlayer->GetTeam() &&
					(pTarget->m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI ||
					 pTarget->m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER))
					EnterBattlefieldPassenger(pTarget);

				// set his velocity to fast upward (for now)
				if(length(pTarget->m_Pos-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*m_ProximityRadius*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);

				vec2 Dir;
				if (length(pTarget->m_Pos - m_Pos) > 0.0f)
					Dir = normalize(pTarget->m_Pos - m_Pos);
				else
					Dir = vec2(0.f, -1.f);

				pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
					m_pPlayer->GetCID(), m_ActiveWeapon);
				Hits++;
			}

			// if we Hit anything, we have to wait for the reload
			if(Hits)
			{
				m_ReloadTimer = Server()->TickSpeed()/3;
				return;
			}

			if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_MINI)
			{
				new CBomb(GameWorld(), m_Pos, m_pPlayer->GetCID(),
					vec2((float)m_LatestInput.m_TargetX,
						(float)m_LatestInput.m_TargetY),
					CBomb::TYPE_GRENADE, 1);
				m_ReloadTimer = max(1, round_to_int(Server()->TickSpeed()*0.4f));
			}
			else if(m_pPlayer->IsSniper())
			{
				m_Invisible = !m_Invisible;
				GameServer()->SendChatTarget(m_pPlayer->GetCID(), m_Invisible ?
					"You are now invisible." : "You are no longer invisible.");
			}
			else if(m_pPlayer->IsEngineer() && m_NumMines < 3 &&
				m_MinesSinceSupply < 3)
			{
				new CMine(GameWorld(), m_Pos, m_pPlayer->GetCID());
				m_NumMines++;
				m_MinesSinceSupply++;
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Mine placed %i/3!.", m_pPlayer->GetCID()), m_NumMines);
				GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
				GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_MINE,
					"Mine tip: max 3 active. Restock at supply stations.");
			}
			else if(m_pPlayer->IsMedic())
			{
				new CHealthShot(GameWorld(), m_Pos, Direction, m_pPlayer->GetCID(), 1);
				m_ReloadTimer = max(1, round_to_int(Server()->TickSpeed()*0.7f));
			}
			else if(m_pPlayer->IsSoldier() && m_NumAmmoPacks < 2 &&
				m_DeployCooldown == 0)
			{
				new CAmmo(GameWorld(), m_Pos, m_pPlayer->GetCID());
				m_NumAmmoPacks++;
				m_DeployCooldown = Server()->TickSpeed()*5;
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Ammo-Pack placed %i/2!.", m_pPlayer->GetCID()), m_NumAmmoPacks);
				GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
				GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_AMMO_PACK,
					"Ammo-Pack tip: max 2 deployed. Allies (and you) can restock from them.");
			}

		} break;

		case WEAPON_GUN:
		{
			if(m_BattlefieldVehiclePassenger &&
				m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER)
			{
				CProjectile *pProjectile = new CProjectile(GameWorld(), WEAPON_GUN,
					m_pPlayer->GetCID(), ProjStartPos, Direction*2.0f,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
					1, false, 22, 0.0f, -1, WEAPON_GUN);
				SendBattlefieldExtraProjectile(pProjectile);
				m_ReloadTimer = 10;
				GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
				break;
			}

			if((m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI &&
				(!m_pBattlefieldPassenger || m_BattlefieldVehiclePassenger)) ||
				m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_SHIP)
			{
				CProjectile *pProjectile = new CProjectile(GameWorld(), WEAPON_GUN,
					m_pPlayer->GetCID(), ProjStartPos, Direction,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
					1, true, 3, 0.0f, -1, WEAPON_GUN);
				SendBattlefieldExtraProjectile(pProjectile);
				m_ReloadTimer = 10;
				GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
				break;
			}

			if(m_pPlayer->IsEngineer())
			{
				m_EngineerAction = true;
				vec2 ToolPos = m_Pos+Direction*60.0f;
				vec2 HitPos;
				if(GameServer()->Collision()->IntersectLine(m_Pos, ToolPos, &HitPos, 0))
				{
					ToolPos = HitPos;
					if(m_EngineerToolCooldown == 0)
					{
						GameServer()->CreateDamageInd(ToolPos, GetAngle(Direction)+pi, 1);
						m_EngineerToolCooldown = max(1, round_to_int(Server()->TickSpeed()*0.1f));
					}
				}
				CCharacter *pTarget = GameWorld()->ClosestCharacter(ToolPos,
					m_ProximityRadius, this);
				if(pTarget && pTarget->IsAlive() && m_EngineerToolCooldown == 0 &&
					!GameServer()->Collision()->IntersectLine(m_Pos, pTarget->m_Pos, 0, 0))
				{
					pTarget->TakeDamage(vec2(0, 0), 1, m_pPlayer->GetCID(), WEAPON_GUN);
					m_EngineerToolCooldown = max(1, round_to_int(Server()->TickSpeed()*0.1f));
				}
				GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND);
				break;
			}

			vec2 GunDirection = Direction;
			int CustomType = 1;
			if(m_pPlayer->IsMedic())
			{
				GunDirection *= 2.0f;
				CustomType = 22;
			}
			else if(m_pPlayer->IsSoldier())
				GunDirection *= 1.5f;

			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				GunDirection,
				(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
				1, false, CustomType, 0.0f, -1, WEAPON_GUN);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(1);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);

			Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		} break;

		case WEAPON_SHOTGUN:
		{
			if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI &&
				!m_BattlefieldVehiclePassenger && m_pBattlefieldPassenger)
			{
				new CLaser(GameWorld(), ProjStartPos, Direction, 1000.0f,
					m_pPlayer->GetCID(), 2);
				GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
				if(m_BattlefieldVehicleBurstShots == 5)
				{
					m_BattlefieldVehicleBurstShots = 0;
					m_ReloadTimer = 30;
				}
				else
				{
					m_BattlefieldVehicleBurstShots++;
					m_ReloadTimer = 3;
				}
				break;
			}

			int ShotSpread = 2;

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
				float a = GetAngle(Direction);
				a += Spreading[i+2];
				float v = 1-(absolute(i)/(float)ShotSpread);
				float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
				int Damage = m_pPlayer->IsMedic() ? 2 : 1;
				int CustomType = m_pPlayer->IsMedic() ? 5 : 1;
				CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
					m_pPlayer->GetCID(),
					ProjStartPos,
					vec2(cosf(a), sinf(a))*Speed,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
					Damage, false, CustomType, 0.0f, -1, WEAPON_SHOTGUN);

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);

				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
			}

			Server()->SendMsg(&Msg, 0,m_pPlayer->GetCID());

			GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
		} break;

		case WEAPON_GRENADE:
		{
			if(!m_BattlefieldVehiclePassenger &&
				m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER)
			{
				vec2 Start = m_pBattlefieldPassenger ? m_Pos+Direction*3.0f : ProjStartPos;
				if(GameServer()->Collision()->IntersectLine(Start, m_Pos, 0, 0))
					Start = m_Pos+Direction*m_ProximityRadius*0.5f;
				CProjectile *pProjectile = new CProjectile(GameWorld(), WEAPON_GRENADE,
					m_pPlayer->GetCID(), Start, Direction*1.7f,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
					1, true, 2, 0.0f, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);
				SendBattlefieldExtraProjectile(pProjectile);
				m_ReloadTimer = max(1, round_to_int(Server()->TickSpeed()*1.5f));
				GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			}
			else if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_UBOAT)
			{
				if(!m_BattlefieldVehicleFireToggle)
				{
					m_BattlefieldVehicleFireToggle = true;
					CProjectile *pProjectile = new CProjectile(GameWorld(), WEAPON_GRENADE,
						m_pPlayer->GetCID(), ProjStartPos, Direction*1.3f,
						(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
						1, true, 7, 0.0f, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);
					SendBattlefieldExtraProjectile(pProjectile);
					GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
				}
				else
				{
					ResetBattlefieldUboatShot();
					m_ReloadTimer = Server()->TickSpeed()*2;
				}
			}
			else if(m_pPlayer->IsSoldier())
			{
				if(m_NumC4 >= 3)
				{
					GameServer()->SendChatTarget(m_pPlayer->GetCID(), "Can't place more C4!");
					break;
				}
				new CC4(GameWorld(), m_Pos, m_pPlayer->GetCID(), Direction*5.0f);
				m_NumC4++;
				m_C4SinceSupply++;
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf),
					GameServer()->Localize("C4 placed %i/3!. Press right-mouse to explode!", m_pPlayer->GetCID()), m_NumC4);
				GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
				GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_C4,
					"C4 tip: max 3. Right-mouse detonates. Restock grenades at supply.");
				GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			}
			else if(m_pPlayer->IsEngineer())
			{
				int CustomType = m_EngineerGrenadeMode ? 9 : 8;
				m_EngineerGrenadeMode = !m_EngineerGrenadeMode;
				new CGuidedProjectile(GameWorld(), WEAPON_GRENADE, m_pPlayer->GetCID(),
					ProjStartPos, Direction,
					(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
					1, true, CustomType, 0.0f, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);
				m_ReloadTimer = Server()->TickSpeed()*2;
				GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			}
		} break;

		case WEAPON_RIFLE:
		{
			new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
		} break;

		case WEAPON_NINJA:
		{
			// reset Hit objects
			m_NumObjectsHit = 0;

			m_Ninja.m_ActivationDir = Direction;
			m_Ninja.m_CurrentMoveTime =
				g_pData->m_Weapons.m_Ninja.m_Movetime*Server()->TickSpeed()/3000;
			m_Ninja.m_OldVelAmount = round_to_int(length(m_Core.m_Vel));
			m_NinjaSwitchCooldown = 150;
			m_ReloadTimer = 10;

			GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE);
		} break;

	}

	m_AttackTick = Server()->Tick();

	bool ConsumeWeaponAmmo = true;
	if(m_ActiveWeapon == WEAPON_GUN)
	{
		if(m_pPlayer->IsSoldier() && !InBattlefieldVehicle())
		{
			m_SoldierGunMagazine--;
			ConsumeWeaponAmmo = false;
		}
		else if(m_pPlayer->IsMedic() && !InBattlefieldVehicle())
		{
			m_ClassGunMagazine--;
			ConsumeWeaponAmmo = false;
		}
		else if(m_pPlayer->IsEngineer() &&
			(!InBattlefieldVehicle() ||
			 m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_CAR))
		{
			// The Engineer tool follows the same custom counter branch as
			// Medic, but Engineer never refills it. It may run
			// negative and deliberately leaves ordinary Gun ammo untouched.
			m_ClassGunMagazine--;
			ConsumeWeaponAmmo = false;
		}
	}

	if(ConsumeWeaponAmmo && !m_InAntiAircraftCannon &&
		m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	if(!m_ReloadTimer)
	{
		if(m_pPlayer->IsEngineer() && m_ActiveWeapon == WEAPON_GUN)
			return;
		int Divisor = m_pPlayer->IsSoldier() ? 1500 :
			m_pPlayer->IsMedic() ? 600 : 1000;
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay*
			Server()->TickSpeed()/Divisor;
	}
}

void CCharacter::HandleWeapons()
{
	// JetFly forces the visible weapon to Ninja, but the occupied Jet uses that
	// slot only as its vehicle representation. Running HandleNinja here can
	// consume a pending Ninja move and replace the forced weapon before
	// FireWeapon sees the Jet's Fire input. Bypass HandleNinja while Jet is set.
	if(m_pPlayer->IsSniper() &&
		m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_JET)
		HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();
}

void CCharacter::HandleTicks()
{
	if(m_BroadcastClearTimer > 0)
		m_BroadcastClearTimer--;
	if(m_BattlefieldTileCooldown > 0)
		m_BattlefieldTileCooldown--;
	if(m_DeployCooldown > 0)
		m_DeployCooldown--;
	if(m_SelfAmmoCooldown > 0)
		m_SelfAmmoCooldown--;
	if(m_NinjaSwitchCooldown > 0)
		m_NinjaSwitchCooldown--;
	if(m_GodModeTimer > 0)
		m_GodModeTimer--;
	if(m_BattlefieldVehicleEntryCooldown > 0)
		m_BattlefieldVehicleEntryCooldown--;
	if(m_EngineerToolCooldown > 0)
		m_EngineerToolCooldown--;
	if(m_BattlefieldRepairCooldown > 0)
		m_BattlefieldRepairCooldown--;
	if(m_EngineerObstructedCooldown > 0)
		m_EngineerObstructedCooldown--;
	if(m_OccupiedVehicleAmmoRegenCooldown > 0)
		m_OccupiedVehicleAmmoRegenCooldown--;
	if(m_MedicHealCooldown > 0)
		m_MedicHealCooldown--;
	if(m_MedicHealSecondCooldown > 0)
		m_MedicHealSecondCooldown--;
	if(m_BattlefieldVehicleGrace > 0)
		m_BattlefieldVehicleGrace--;
	if(m_HeliPassengerEntryDelay > 0)
		m_HeliPassengerEntryDelay--;
	if(m_AntiTankFireCooldown > 0)
		m_AntiTankFireCooldown--;
}

void CCharacter::HandleTiles()
{
	if(m_pPlayer->IsSniper() && (m_Invisible ||
		(m_ActiveWeapon == WEAPON_HAMMER && m_InvisibilityPower < Server()->TickSpeed()*5)))
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Invisiblepower: %i | %i", m_pPlayer->GetCID()),
			m_InvisibilityPower, Server()->TickSpeed()*5);
		GameServer()->SendBroadcast(aBuf, m_pPlayer->GetCID());
		m_BroadcastClearTimer = 5;
	}

	HandleBattlefieldTiles();

	// Invoke water handling only on an actual water tile, or once for each
	// water side touching TILE_BF_NOFLAG (shoreline). Calling it for every
	// ordinary tile incorrectly applied the non-water 0.9 velocity cap to all
	// infantry movement.
	int TileIndex = GameServer()->Collision()->GetIndex(m_BattlefieldTileLastPos, m_Pos);
	bool OnWaterTile = false;
	if(TileIndex >= 0)
	{
		int Tile = GameServer()->Collision()->GetTileIndex(TileIndex);
		if(Tile == TILE_BF_NOFLAG)
		{
			if(GameServer()->Collision()->isWater(round_to_int(m_Pos.x)-50, round_to_int(m_Pos.y)))
				HandleBattlefieldWater();
			if(GameServer()->Collision()->isWater(round_to_int(m_Pos.x)+50, round_to_int(m_Pos.y)))
				HandleBattlefieldWater();
		}
		else if(Tile == TILE_WATER)
		{
			HandleBattlefieldWater();
			OnWaterTile = true;
		}
	}
	m_InBattlefieldWater = OnWaterTile;
	if(!OnWaterTile)
	{
		m_InBattlefieldWater = false;
		if(!InBattlefieldVehicle())
		{
			m_Armor = 0;
			m_BattlefieldOxygenActive = false;
		}
		m_HarpoonCharge = 0;
		m_HarpoonCharging = false;
	}
}

void CCharacter::HandleOther()
{
	if(InBattlefieldVehicle() &&
		m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_MINI &&
		m_BattlefieldVehicleHealth < 0)
	{
		if(m_BattlefieldVehiclePassenger)
			Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		else
		{
			LeaveBattlefieldVehicle(true);
			Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		}
	}

	// Apply the six solid probes only to the standard
	// 28-pixel character body. Each positive/negative/center hit shifts the
	// corresponding core coordinate by one in the negative direction.
	if(m_ProximityRadius == 28.0f)
	{
		if(GameServer()->Collision()->CheckPoint(m_Pos.x+14.0f, m_Pos.y))
			m_Core.m_Pos.x -= 1.0f;
		if(GameServer()->Collision()->CheckPoint(m_Pos.x-14.0f, m_Pos.y))
			m_Core.m_Pos.x -= 1.0f;
		if(GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y))
			m_Core.m_Pos.x -= 1.0f;
		if(GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y+14.0f))
			m_Core.m_Pos.y -= 1.0f;
		if(GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y-14.0f))
			m_Core.m_Pos.y -= 1.0f;
		if(GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y))
			m_Core.m_Pos.y -= 1.0f;
	}

	// Clear an expiring broadcast here, before class and vehicle
	// status branches may publish a replacement in the same HandleOther pass.
	if(m_BroadcastClearTimer == 1)
		GameServer()->SendBroadcast("", m_pPlayer->GetCID());

	// Soldier and Medic consume their displayed Gun ammo once per custom-sized
	// magazine, not once per shot. HandleOther performs the magazine rollover
	// before HandleWeapons on the following tick.
	if(m_pPlayer->IsSoldier())
	{
		if(m_SoldierGunMagazine == 0)
		{
			m_aWeapons[WEAPON_GUN].m_Ammo--;
			m_SoldierGunMagazine = 12;
			m_ReloadTimer = Server()->TickSpeed()*2;
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		}
		m_aWeapons[WEAPON_GRENADE].m_Ammo = 3-m_C4SinceSupply;
		if(!InBattlefieldVehicle() &&
			(m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE))
			m_ActiveWeapon = WEAPON_GUN;
	}

	if(HandleBattlefieldMedicAbilities())
		return;
	HandleBattlefieldEngineerAbilities();

	if(m_pPlayer->IsSniper() && !InBattlefieldVehicle() &&
		(m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_GRENADE))
		m_ActiveWeapon = WEAPON_RIFLE;

	if(m_pPlayer->IsMedic())
	{
		if(m_ClassGunMagazine == 0)
		{
			m_aWeapons[WEAPON_GUN].m_Ammo--;
			m_ClassGunMagazine = 50;
			m_ReloadTimer = Server()->TickSpeed()*2;
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		}
		if(!InBattlefieldVehicle() &&
			(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_RIFLE))
			m_ActiveWeapon = WEAPON_GUN;
	}

	HandleBattlefieldSniperAbilities();

	if(!InBattlefieldVehicle())
	{
		if(m_InAntiAircraftCannon)
			m_ActiveWeapon = WEAPON_GUN;
	}

	// Expose occupied vehicle health through the vanilla armor
	// field. Mini is the only occupied type omitted from this branch.
	if(InBattlefieldVehicle() &&
		m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_MINI)
	{
		m_BattlefieldVehicleHealth = min(m_BattlefieldVehicleHealth,
			CBattle::MaxHealth(m_BattlefieldVehicleType));
		m_Armor = m_BattlefieldVehicleHealth/3;
	}

	// Replenish the selected infantry weapon while
	// occupying the vehicle seats whose weapons consume that shared ammo. The
	// Panzer gunner uses the shorter 14-tick interval; an uncrewed Heli pilot,
	// Heli gunner and Ship pilot use 23 ticks. The ammo remains replenished when
	// the character later leaves the vehicle.
	int RegenInterval = 0;
	if(m_BattlefieldVehiclePassenger &&
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER)
		RegenInterval = 14;
	else if((m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI &&
		(m_BattlefieldVehiclePassenger || !m_pBattlefieldPassenger)) ||
		(!m_BattlefieldVehiclePassenger &&
		 m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_SHIP))
		RegenInterval = 23;

	if(RegenInterval && m_OccupiedVehicleAmmoRegenCooldown == 0)
	{
		m_aWeapons[m_ActiveWeapon].m_Ammo =
			min(m_aWeapons[m_ActiveWeapon].m_Ammo+1, 10);
		m_OccupiedVehicleAmmoRegenCooldown = RegenInterval;
	}

	// Jet publishes its separate speed/health status in JetFly and Mini has no
	// vehicle status message. Every other occupied vehicle state refreshes this
	// exact string on each HandleOther pass.
	if(InBattlefieldVehicle() &&
		m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_JET &&
		m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_MINI)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Vehicle-Health: %i/%i", m_pPlayer->GetCID()),
			m_BattlefieldVehicleHealth,
			CBattle::MaxHealth(m_BattlefieldVehicleType));
		GameServer()->SendBroadcast(aBuf, m_pPlayer->GetCID());
		m_BroadcastClearTimer = 25;
	}
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = min(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
}

bool CCharacter::RefillBattlefieldWaterAmmo()
{
	bool AuxiliaryChanged = false;
	if(m_pPlayer->HasHarpoon() && m_HarpoonAmmo < 10)
	{
		m_HarpoonAmmo = 10;
		AuxiliaryChanged = true;
	}
	if(m_pPlayer->HasArrows() && m_ArrowAmmo < 15)
	{
		m_ArrowAmmo = 15;
		AuxiliaryChanged = true;
	}
	if(m_pPlayer->HasThrowingStar() && m_ThrowingStarAmmo < 5)
	{
		m_ThrowingStarAmmo = 5;
		AuxiliaryChanged = true;
	}
	return AuxiliaryChanged;
}

bool CCharacter::RefillBattlefieldAmmo(bool StationarySupply)
{
	bool AuxiliaryChanged = RefillBattlefieldWaterAmmo();
	bool HandGrenadeNeeded = !m_HandGrenadeAvailable;
	if(HandGrenadeNeeded)
	{
		m_HandGrenadeAvailable = true;
		AuxiliaryChanged = true;
	}

	bool ClassSupplyTriggered = false;
	if(m_pPlayer->IsSoldier())
	{
		ClassSupplyTriggered = m_aWeapons[WEAPON_GUN].m_Ammo < 6 ||
			m_C4SinceSupply > 0 || HandGrenadeNeeded;
		if(ClassSupplyTriggered)
		{
			m_aWeapons[WEAPON_GUN].m_Ammo = 6;
			m_C4SinceSupply = 0;
		}
	}
	else if(m_pPlayer->IsEngineer())
	{
		ClassSupplyTriggered = m_aWeapons[WEAPON_GUN].m_Ammo < 10 ||
			m_aWeapons[WEAPON_SHOTGUN].m_Ammo < 10 ||
			m_aWeapons[WEAPON_GRENADE].m_Ammo < 10 ||
			m_MinesSinceSupply > 0 || HandGrenadeNeeded;
		if(ClassSupplyTriggered)
		{
			m_aWeapons[WEAPON_GUN].m_Ammo = 10;
			m_aWeapons[WEAPON_SHOTGUN].m_Ammo = 10;
			m_aWeapons[WEAPON_GRENADE].m_Ammo = 10;
			m_MinesSinceSupply = 0;
		}
	}
	else if(m_pPlayer->IsMedic())
	{
		ClassSupplyTriggered = m_aWeapons[WEAPON_GUN].m_Ammo < 3 ||
			m_aWeapons[WEAPON_SHOTGUN].m_Ammo < 10 ||
			HandGrenadeNeeded;
		if(ClassSupplyTriggered)
		{
			m_aWeapons[WEAPON_GUN].m_Ammo = 2;
			m_aWeapons[WEAPON_SHOTGUN].m_Ammo = 10;
		}
	}
	else if(m_pPlayer->IsSniper())
	{
		ClassSupplyTriggered = m_aWeapons[WEAPON_GUN].m_Ammo < 10 ||
			m_aWeapons[WEAPON_RIFLE].m_Ammo < 10 ||
			HandGrenadeNeeded;
		if(ClassSupplyTriggered)
		{
			m_aWeapons[WEAPON_GUN].m_Ammo = 10;
			m_aWeapons[WEAPON_RIFLE].m_Ammo = 10;
		}
	}

	// A placed pack only plays the pickup sound for the class/hand-grenade
	// branch; a stationary supply also activates for water-ammo-only changes.
	return ClassSupplyTriggered || (StationarySupply && AuxiliaryChanged);
}

void CCharacter::ApplyBattlefieldLoadout()
{
	if(!m_pPlayer)
		return;

	if(m_pPlayer->HasHarpoon())
		m_HarpoonAmmo = 10;
	if(m_pPlayer->HasArrows())
		m_ArrowAmmo = 15;
	if(m_pPlayer->HasThrowingStar())
		m_ThrowingStarAmmo = 5;

	if(!m_pPlayer->HasBattlefieldClass())
		return;

	if(m_pPlayer->IsSoldier())
	{
		m_SoldierGunMagazine = 12;
		m_aWeapons[WEAPON_GUN].m_Got = true;
		m_aWeapons[WEAPON_GUN].m_Ammo = 6;
		m_aWeapons[WEAPON_SHOTGUN].m_Got = false;
		m_aWeapons[WEAPON_GRENADE].m_Got = true;
		m_aWeapons[WEAPON_RIFLE].m_Got = false;
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_aWeapons[WEAPON_NINJA].m_Ammo = 0;
	}
	else if(m_pPlayer->IsEngineer())
	{
		m_aWeapons[WEAPON_GUN].m_Got = true;
		m_aWeapons[WEAPON_GUN].m_Ammo = 10;
		m_aWeapons[WEAPON_SHOTGUN].m_Got = true;
		m_aWeapons[WEAPON_SHOTGUN].m_Ammo = 10;
		m_aWeapons[WEAPON_GRENADE].m_Got = true;
		m_aWeapons[WEAPON_GRENADE].m_Ammo = 10;
		m_aWeapons[WEAPON_RIFLE].m_Got = false;
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_aWeapons[WEAPON_NINJA].m_Ammo = 0;
	}
	else if(m_pPlayer->IsMedic())
	{
		m_ClassGunMagazine = 50;
		m_aWeapons[WEAPON_GUN].m_Got = true;
		m_aWeapons[WEAPON_GUN].m_Ammo = 3;
		m_aWeapons[WEAPON_SHOTGUN].m_Got = true;
		m_aWeapons[WEAPON_SHOTGUN].m_Ammo = 10;
		m_aWeapons[WEAPON_GRENADE].m_Got = false;
		m_aWeapons[WEAPON_RIFLE].m_Got = false;
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_aWeapons[WEAPON_NINJA].m_Ammo = 0;
	}
	else if(m_pPlayer->IsSniper())
	{
		m_aWeapons[WEAPON_GUN].m_Got = true;
		m_aWeapons[WEAPON_GUN].m_Ammo = 10;
		m_aWeapons[WEAPON_SHOTGUN].m_Got = false;
		m_aWeapons[WEAPON_GRENADE].m_Got = false;
		m_aWeapons[WEAPON_RIFLE].m_Got = true;
		m_aWeapons[WEAPON_RIFLE].m_Ammo = 10;
		m_aWeapons[WEAPON_NINJA].m_Got = true;
		m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
		m_Ninja.m_ActivationTick = Server()->Tick();
	}

	if(!m_aWeapons[m_ActiveWeapon].m_Got)
		m_ActiveWeapon = WEAPON_GUN;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;
}

void CCharacter::ReleaseMineSlot()
{
	m_NumMines--;
	Act();
}

bool CCharacter::WantsC4Detonation() const
{
	return m_pPlayer && m_pPlayer->IsSoldier() &&
		m_ActiveWeapon == WEAPON_GRENADE && m_Input.m_Hook != 0;
}

void CCharacter::StartAmmoSupplyCooldown()
{
	m_SelfAmmoCooldown = Server()->TickSpeed()*10;
}

void CCharacter::ReleaseC4Slot()
{
	m_NumC4--;
	Act();
}

void CCharacter::ReleaseHealthKitSlot()
{
	m_NumHealthKits--;
	Act();
}

void CCharacter::ReleaseAmmoPackSlot()
{
	m_NumAmmoPacks--;
	Act();
}

void CCharacter::Act()
{
	if(!m_pPlayer)
		return;

	char aBuf[128];
	if(m_pPlayer->IsEngineer())
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Mines now aviable: %i/3!.", m_pPlayer->GetCID()), 3-m_NumMines);
	else if(m_pPlayer->IsMedic())
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Health-Kit now aviable %i/1!.", m_pPlayer->GetCID()), 1-m_NumHealthKits);
	else if(m_pPlayer->IsSoldier())
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Ammo-Pack now aviable %i/2!.", m_pPlayer->GetCID()), 2-m_NumAmmoPacks);
	else
		return;
	GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
}

void CCharacter::RegisterBattlefieldKill(bool CountKill)
{
	if(!m_Alive || !m_pPlayer)
		return;
	if(CountKill)
		m_KillStreak++;
	if(m_KillStreak != 5 && m_KillStreak != 10 && m_KillStreak != 15 &&
		m_KillStreak != 20 && m_KillStreak != 30)
		return;

	m_GodModeTimer = Server()->TickSpeed()*5;
	const char *pTitle = m_KillStreak == 5 ? "on a Killing Spree" :
		m_KillStreak == 10 ? "on Ramapage" :
		m_KillStreak == 15 ? "INSANE" :
		m_KillStreak == 20 ? "GODLIKE" : "an ICE COLD KILLER";
	char aBuf[256];
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameServer()->m_apPlayers[i])
			continue;
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("%s is %s and got 5 sec God-Mode!", i),
			Server()->ClientName(m_pPlayer->GetCID()),
			GameServer()->Localize(pTitle, i));
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = -1;
		Msg.m_pMessage = aBuf;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
	GameServer()->SendChatTarget(m_pPlayer->GetCID(),
		"You got 5 sec God-Mod! Good job!");
	for(int Slot = 1; Slot <= 3; Slot++)
		new CShield(GameWorld(), m_Pos, m_pPlayer->GetCID(), 4, true, Slot);
}

void CCharacter::HandGrenade()
{
	if(!m_Alive || !m_pPlayer)
		return;
	if(InBattlefieldVehicle())
	{
		// Emoticon while occupied sets the same vehicle reset flag as /e and
		// the water/Stop paths.
		m_BattlefieldVehicleWaterResetPending = true;
		return;
	}
	if(m_InAntiTankCannon)
	{
		LeaveAntiTankCannon(true);
		return;
	}
	if(m_InAntiAircraftCannon)
	{
		LeaveAntiAircraftCannon(true);
		return;
	}
	if(m_HasSmokeLauncher)
	{
		m_SmokeLauncherFireRequested = true;
		return;
	}
	if(!m_HandGrenadeAvailable)
		return;

	vec2 Direction = SafeNormalize(vec2((float)m_LatestInput.m_TargetX,
		(float)m_LatestInput.m_TargetY));
	CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
		m_pPlayer->GetCID(), m_Pos, Direction*0.7f,
		(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
		1, true, 23, 0.0f, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);
	CNetObj_Projectile Projectile;
	pProj->FillInfo(&Projectile);
	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(1);
	for(unsigned i = 0; i < sizeof(Projectile)/sizeof(int); i++)
		Msg.AddInt(reinterpret_cast<int *>(&Projectile)[i]);
	Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
	GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH);
	m_HandGrenadeAvailable = false;
	GameServer()->SendChatTarget(m_pPlayer->GetCID(),
		"Hand-Grenade fired, get an ammopack for new one!.");
}

void CCharacter::HandleBattlefieldExitCommand()
{
	if(!m_Alive)
		return;
	if(InBattlefieldVehicle())
	{
		// /e sets the shared vehicle reset flag. Vehicle routines consume it
		// after HandleWeapons rather than creating an abandoned wreck.
		m_BattlefieldVehicleWaterResetPending = true;
		return;
	}
	HandGrenade();
}

void CCharacter::BlockBattlefieldTile(int Flags, const char *pMessage)
{
	// Every Stop variant blocks ninja movement. HandleNinja observes it later
	// in the same Tick and suppresses the remainder of the current dash.
	m_NinjaMovementBlocked = true;

	if(pMessage)
	{
		GameServer()->SendBroadcast(pMessage, m_pPlayer->GetCID());
		m_BroadcastClearTimer = 20;
	}

	bool Stopped = false;
	if(Flags == 0)
	{
		if(m_Core.m_Vel.y >= 0.0f)
		{
			m_Core.m_Vel.y = 0.0f;
			m_Core.m_Pos.y = m_BattlefieldTileLastPos.y;
			Stopped = true;
		}
	}
	else if(Flags == TILEFLAG_ROTATE)
	{
		if(m_Core.m_Vel.x <= 0.0f)
		{
			m_Core.m_Vel.x = 0.0f;
			m_Core.m_Pos.x = m_BattlefieldTileLastPos.x;
			Stopped = true;
		}
	}
	else if(Flags == (TILEFLAG_VFLIP|TILEFLAG_HFLIP))
	{
		if(m_Core.m_Vel.y <= 0.0f)
		{
			m_Core.m_Vel.y = 0.0f;
			m_Core.m_Pos.y = m_BattlefieldTileLastPos.y;
			Stopped = true;
		}
	}
	else if(Flags == (TILEFLAG_VFLIP|TILEFLAG_HFLIP|TILEFLAG_ROTATE))
	{
		if(m_Core.m_Vel.x >= 0.0f)
		{
			m_Core.m_Vel.x = 0.0f;
			m_Core.m_Pos.x = m_BattlefieldTileLastPos.x;
			Stopped = true;
		}
	}
	else
	{
		m_Core.m_Vel = vec2(0, 0);
		m_Core.m_Pos = m_BattlefieldTileLastPos;
		Stopped = true;
	}

	// Every directional Stop that clamps an occupied vehicle also sets the
	// shared vehicle reset flag and restores the previous weapon.
	if(Stopped && InBattlefieldVehicle())
	{
		m_BattlefieldVehicleWaterResetPending = true;
		m_ActiveWeapon = m_LastWeapon;
	}
}

void CCharacter::SelectBattlefieldClass(int Class)
{
	const char *pName = "";
	if(Class == CPlayer::BATTLEFIELD_CLASS_SOLDIER)
		pName = "Soldier";
	else if(Class == CPlayer::BATTLEFIELD_CLASS_ENGINEER)
		pName = "Engineer";
	else if(Class == CPlayer::BATTLEFIELD_CLASS_MEDIC)
		pName = "Medic";
	else if(Class == CPlayer::BATTLEFIELD_CLASS_SNIPER)
		pName = "Sniper";
	else
		return;

	char aBuf[128];
	if(m_pPlayer->GetBattlefieldClass() == Class)
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("You are already %s.", m_pPlayer->GetCID()),
			GameServer()->Localize(pName, m_pPlayer->GetCID()));
	else
	{
		m_pPlayer->SetBattlefieldClass(Class);
		ApplyBattlefieldLoadout();
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("You are now %s! Good luck.", m_pPlayer->GetCID()),
			GameServer()->Localize(pName, m_pPlayer->GetCID()));
		GameServer()->SendSixupClientInfoUpdate(m_pPlayer->GetCID());
		GameServer()->CreatePlayerSpawn(m_Pos);
		if(Class == CPlayer::BATTLEFIELD_CLASS_SOLDIER)
			GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_CLASS_HINT,
				"Soldier tip: Hammer deploys ammo packs; Grenade places C4; emote or /e throws a hand grenade.");
		else if(Class == CPlayer::BATTLEFIELD_CLASS_ENGINEER)
			GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_CLASS_HINT,
				"Engineer tip: Hammer places mines; Gun repairs vehicles and hacks enemy doors.");
		else if(Class == CPlayer::BATTLEFIELD_CLASS_MEDIC)
			GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_CLASS_HINT,
				"Medic tip: Hammer fires heal shots; you slowly regenerate and can heal teammates.");
		else if(Class == CPlayer::BATTLEFIELD_CLASS_SNIPER)
			GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_CLASS_HINT,
				"Sniper tip: Hammer toggles invisibility; switch to Hammer to regenerate invis power.");
	}
	GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
	m_BattlefieldTileCooldown = Server()->TickSpeed();
}

void CCharacter::SelectWaterWeapon(int Weapon)
{
	const char *pDescription = 0;
	int Ammo = 0;
	if(Weapon == CPlayer::BATTLEFIELD_WATER_WEAPON_HARPOON)
	{
		pDescription = "Water-Weapon = Harpoon. Only usable in water. Press Spacebar to fire it.";
		Ammo = 10;
	}
	else if(Weapon == CPlayer::BATTLEFIELD_WATER_WEAPON_ARROWS)
	{
		pDescription = "Water-Weapon = Arrows. Only usable in water. Press Spacebar to fire it.";
		Ammo = 15;
	}
	else if(Weapon == CPlayer::BATTLEFIELD_WATER_WEAPON_THROWING_STAR)
	{
		pDescription = "Water-Weapon = Throwing-Star. Only usable in water. Press Spacebar to fire it.";
		Ammo = 5;
	}
	else
		return;

	if(m_pPlayer->GetWaterWeapon() == Weapon)
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), "You have already this Water-Weapon.");
	else
	{
		m_pPlayer->SetWaterWeapon(Weapon);
		m_HarpoonAmmo = Weapon == CPlayer::BATTLEFIELD_WATER_WEAPON_HARPOON ? Ammo : 0;
		m_ArrowAmmo = Weapon == CPlayer::BATTLEFIELD_WATER_WEAPON_ARROWS ? Ammo : 0;
		m_ThrowingStarAmmo = Weapon == CPlayer::BATTLEFIELD_WATER_WEAPON_THROWING_STAR ? Ammo : 0;
		GameServer()->CreatePlayerSpawn(m_Pos);
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), pDescription);
	}
	m_BattlefieldTileCooldown = Server()->TickSpeed();
}

void CCharacter::HandleBattlefieldTiles()
{
	int TileIndex = GameServer()->Collision()->GetIndex(m_BattlefieldTileLastPos, m_Pos);
	if(TileIndex < 0)
	{
		LeaveAntiAircraftCannon();
		LeaveAntiTankCannon();
		return;
	}

	int Tile = GameServer()->Collision()->GetTileIndex(TileIndex);
	int Flags = GameServer()->Collision()->GetTileFlags(TileIndex);
	HandleAntiAircraftCannon(Tile, TileIndex);
	HandleAntiTankCannon(Tile, TileIndex);

	if(Tile >= TILE_BF_CHECKPOINT1 && Tile <= TILE_BF_CHECKPOINT3)
	{
		int State = GameServer()->m_aCheckpointState[Tile-TILE_BF_CHECKPOINT1];
		if((m_pPlayer->GetTeam() == TEAM_RED && State > 0) ||
			(m_pPlayer->GetTeam() == TEAM_BLUE && State < 0))
			BlockBattlefieldTile(Flags, "Your Team hasn't this Checkpoint!");
	}
	else if(Tile == TILE_BF_REQUIRE_CLASS && !m_pPlayer->HasBattlefieldClass())
		BlockBattlefieldTile(Flags, "Choose a class first!");
	else if(Tile == TILE_BF_BASE_RED && m_pPlayer->GetTeam() == TEAM_RED)
		BlockBattlefieldTile(Flags, "That isn't your Base!");
	else if(Tile == TILE_BF_BASE_BLUE && m_pPlayer->GetTeam() == TEAM_BLUE)
		BlockBattlefieldTile(Flags, "That isn't your Base!");
	else if(Tile == TILE_BF_CHECK1)
		Check(1);
	else if(Tile == TILE_BF_CHECK2)
		Check(2);
	else if(Tile == TILE_BF_CHECK3)
		Check(3);

	if(m_BattlefieldTileCooldown != 0)
		return;

	if(Tile == TILE_BF_CLASS_SOLDIER)
		SelectBattlefieldClass(CPlayer::BATTLEFIELD_CLASS_SOLDIER);
	else if(Tile == TILE_BF_CLASS_ENGINEER)
		SelectBattlefieldClass(CPlayer::BATTLEFIELD_CLASS_ENGINEER);
	else if(Tile == TILE_BF_CLASS_MEDIC)
		SelectBattlefieldClass(CPlayer::BATTLEFIELD_CLASS_MEDIC);
	else if(Tile == TILE_BF_CLASS_SNIPER)
		SelectBattlefieldClass(CPlayer::BATTLEFIELD_CLASS_SNIPER);
	else if(Tile == TILE_BF_WATER_HARPOON)
		SelectWaterWeapon(CPlayer::BATTLEFIELD_WATER_WEAPON_HARPOON);
	else if(Tile == TILE_BF_WATER_ARROWS)
		SelectWaterWeapon(CPlayer::BATTLEFIELD_WATER_WEAPON_ARROWS);
	else if(Tile == TILE_BF_WATER_STAR)
		SelectWaterWeapon(CPlayer::BATTLEFIELD_WATER_WEAPON_THROWING_STAR);
}

void CCharacter::LeaveAntiAircraftCannon(bool PreserveStationLatch)
{
	m_InAntiAircraftCannon = false;
	m_AntiAircraftBurstCount = 0;
	if(!PreserveStationLatch)
		m_AntiAircraftStationLatched = false;
}

void CCharacter::EjectFromBattlefieldStation()
{
	vec2 ExitPos = m_Core.m_Pos;
	const float Radius = (float)ms_PhysSize;
	if(!GameServer()->Collision()->CheckPoint(ExitPos.x, ExitPos.y-Radius))
		ExitPos.y -= Radius;
	else if(!GameServer()->Collision()->CheckPoint(ExitPos.x+Radius, ExitPos.y))
		ExitPos.x += Radius;
	else if(!GameServer()->Collision()->CheckPoint(ExitPos.x-Radius, ExitPos.y))
		ExitPos.x -= Radius;
	else
		ExitPos.y += Radius;
	m_Core.m_Pos = ExitPos;
	m_Core.m_Vel = vec2(0, 0);
	m_Pos = ExitPos;
}

void CCharacter::HandleAntiAircraftCannon(int Tile, int TileIndex)
{
	if(Tile != TILE_BF_ANTIAIR)
	{
		LeaveAntiAircraftCannon();
		return;
	}

	if(!m_InAntiAircraftCannon)
	{
		if(m_AntiAircraftStationLatched)
		{
			// Clear Anti-Aircraft mount but leave the station latch set.
			// HandleTiles consumes that latch on the following pass and moves
			// the core one physical radius away from the cannon tile.
			m_AntiAircraftStationLatched = false;
			EjectFromBattlefieldStation();
			return;
		}
		LeaveAntiTankCannon();
		m_InAntiAircraftCannon = true;
		m_AntiAircraftStationLatched = true;
		m_AntiAircraftStationPos = GameServer()->Collision()->GetPos(TileIndex);
		m_AntiAircraftBurstCount = 0;
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), "entred Anti-Aircraft Cannon.");
		GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_ANTIAIR,
			"Anti-Air tip: hold fire for bursts; leave with Shift or /e.");
		GameServer()->CreatePlayerSpawn(m_Pos);
	}

	m_Core.m_Pos = m_AntiAircraftStationPos;
	m_Core.m_Vel = vec2(0, 0);
	m_Pos = m_AntiAircraftStationPos;
}

void CCharacter::OnPakDestroyed(CPak *pPak, int Mode)
{
	if(Mode == CPak::MODE_TARGETER && m_pAntiTankTargeter == pPak)
		m_pAntiTankTargeter = 0;
	if(Mode == CPak::MODE_REMOTE_GRENADE && m_pAntiTankRemoteGrenade == pPak)
		m_pAntiTankRemoteGrenade = 0;
}

void CCharacter::ReleaseRemoteGrenadeControl(CPak *pPak)
{
	if(m_pAntiTankRemoteGrenade == pPak)
		m_pAntiTankRemoteGrenade = 0;
}

void CCharacter::LeaveAntiTankCannon(bool PreserveStationLatch)
{
	if(m_pPlayer)
		m_pPlayer->EndBattlefieldAimCamera();
	if(m_pAntiTankTargeter)
	{
		m_pAntiTankTargeter->Reset();
		m_pAntiTankTargeter = 0;
	}
	if(m_pAntiTankRemoteGrenade)
	{
		m_pAntiTankRemoteGrenade->Reset();
		m_pAntiTankRemoteGrenade = 0;
	}
	m_InAntiTankCannon = false;
	if(!PreserveStationLatch)
		m_AntiTankStationLatched = false;
	m_AntiTankTargetCID = -1;
	m_AntiTankFireCooldown = 0;
}

void CCharacter::HandleAntiTankCannon(int Tile, int TileIndex)
{
	if(Tile != TILE_BF_ANTITANK)
	{
		LeaveAntiTankCannon();
		return;
	}

	if(!m_InAntiTankCannon)
	{
		if(m_AntiTankStationLatched)
		{
			// Leave the Anti-Tank station latch set so the next tile pass
			// ejects instead of re-entering.
			m_AntiTankStationLatched = false;
			EjectFromBattlefieldStation();
			return;
		}
		m_InAntiTankCannon = true;
		m_AntiTankStationLatched = true;
		m_AntiTankStationPos = GameServer()->Collision()->GetPos(TileIndex);
		m_AntiTankMode = 1;
		m_AntiTankTargetCID = -1;
		m_AntiTankFireCooldown = 0;
		m_pPlayer->BeginBattlefieldAimCamera();
		GameServer()->SendChatTarget(m_pPlayer->GetCID(),
			"entred Anti-Tank-Cannon, scroll to switch aim-type.");
		GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_ANTITANK,
			"Anti-Tank tip: mouse wheel switches aim control / manual control. Exit: Shift or /e.");
		GameServer()->CreatePlayerSpawn(m_Pos);
	}

	int Switches = CountInput(m_PrevInput.m_NextWeapon, m_Input.m_NextWeapon).m_Presses+
		CountInput(m_PrevInput.m_PrevWeapon, m_Input.m_PrevWeapon).m_Presses;
	if(Switches)
	{
		m_AntiTankMode = m_AntiTankMode == 1 ? 2 : 1;
		m_AntiTankTargetCID = -1;
		if(m_pAntiTankTargeter)
		{
			m_pAntiTankTargeter->Reset();
			m_pAntiTankTargeter = 0;
		}
		if(m_pAntiTankRemoteGrenade)
		{
			m_pAntiTankRemoteGrenade->Reset();
			m_pAntiTankRemoteGrenade = 0;
		}
		GameServer()->SendBroadcast(m_AntiTankMode == 1 ?
			"Switched Type to [aim control]" :
			"Switched Type to [manual control]", m_pPlayer->GetCID());
		m_BroadcastClearTimer = 100;
	}

	bool Fire = CountInput(m_PrevInput.m_Fire, m_Input.m_Fire).m_Presses != 0;
	if(Fire && m_AntiTankFireCooldown == 0)
	{
		if(m_AntiTankMode == 1)
		{
			if(!m_pAntiTankTargeter)
			{
				m_pAntiTankTargeter = new CPak(GameWorld(), m_pPlayer->GetCID(),
					CPak::MODE_TARGETER, GetAntiTankCursor());
			}
			else if(m_AntiTankTargetCID >= 0)
			{
				new CPak(GameWorld(), m_pPlayer->GetCID(), CPak::MODE_GUIDED_MISSILE,
					m_Pos, m_AntiTankTargetCID);
				m_pAntiTankTargeter->Reset();
				m_pAntiTankTargeter = 0;
				m_AntiTankTargetCID = -1;
				m_AntiTankFireCooldown = Server()->TickSpeed()*4;
				GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			}
		}
		else if(!m_pAntiTankRemoteGrenade)
		{
			m_pAntiTankRemoteGrenade = new CPak(GameWorld(), m_pPlayer->GetCID(),
				CPak::MODE_REMOTE_GRENADE, m_Pos);
			m_AntiTankFireCooldown = Server()->TickSpeed()*4;
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		}
	}

	m_Core.m_Pos = m_AntiTankStationPos;
	m_Core.m_Vel = vec2(0, 0);
	m_Pos = m_AntiTankStationPos;
}

void CCharacter::HandleBattlefieldWater()
{
	if(!m_BattlefieldVehiclePassenger && InBattlefieldVehicle() &&
		(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI ||
		 m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_JET ||
		 m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER ||
		 m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_CAR))
		m_BattlefieldVehicleWaterResetPending = true;

	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_UBOAT)
		m_BattlefieldOxygenActive = true;

	if(m_Armor == 0 && !m_BattlefieldOxygenActive)
	{
		m_Armor = 10;
		m_BattlefieldOxygenActive = true;
	}

	if(!InBattlefieldVehicle())
	{
		if(m_Armor == 0 && m_BattlefieldOxygenActive &&
			m_BattlefieldOxygenCooldown == 0)
		{
			if(m_Health < 1)
				Die(m_pPlayer->GetCID(), WEAPON_WORLD);
			m_Health--;
			m_EmoteType = EMOTE_PAIN;
			m_EmoteStop = Server()->Tick()+Server()->TickSpeed()/2;
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
			m_BattlefieldOxygenCooldown = round_to_int(Server()->TickSpeed()*1.5f);
		}
		if(m_BattlefieldOxygenCooldown > 0)
			m_BattlefieldOxygenCooldown--;
		if(m_BattlefieldOxygenCooldown == 0 && m_Armor > 0)
		{
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);
			m_Armor--;
			m_BattlefieldOxygenCooldown = round_to_int(Server()->TickSpeed()*1.5f);
		}
	}

	if(!GameServer()->m_BattlefieldWaterEnabled)
	{
		if(m_Core.m_Vel.x > 6.0f || m_Core.m_Vel.x < -6.0f)
			m_Core.m_Vel.x *= 0.9f;
		if(m_Core.m_Vel.y > 4.5f || m_Core.m_Vel.y < -4.5f)
			m_Core.m_Vel.y *= 0.9f;
		if(m_Core.m_Jumped > 1)
			m_Core.m_Jumped = 1;
	}
	else
	{
		m_Core.m_Jumped |= 3;
		if(m_Input.m_Hook == 0 || InBattlefieldVehicle())
			m_Core.m_Vel.y -= 0.1f;
		else
		{
			m_Core.m_Vel.y -= 0.3f;
			vec2 Direction = normalize(vec2((float)m_Input.m_TargetX,
				(float)m_Input.m_TargetY));
			m_Core.m_Vel += Direction*1.1f;
		}
		if(m_Core.m_Vel.x > 6.0f || m_Core.m_Vel.x < -6.0f)
			m_Core.m_Vel.x *= 0.9f;
		if(m_Core.m_Vel.y > 4.5f || m_Core.m_Vel.y < -4.5f)
			m_Core.m_Vel.y *= 0.9f;
	}

}

void CCharacter::HandleBattlefieldSniperAbilities()
{
	if(m_Invisible)
	{
		m_EmoteType = EMOTE_BLINK;
		m_EmoteStop = Server()->Tick()+round_to_int(Server()->TickSpeed()*0.5f);
		if(m_InvisibilityPower == 0)
		{
			m_Invisible = false;
			GameServer()->SendChatTarget(m_pPlayer->GetCID(),
				"You are no longer invisible! Switch to Hammer to regenerate your Invispower.");
		}
		if(m_InvisibilityPower > 0)
			m_InvisibilityPower--;
	}
	else if(m_pPlayer->IsSniper() && m_InvisibilityPower < 250)
		m_InvisibilityPower++;
}

void CCharacter::HandleBattlefieldEngineerAbilities()
{
	if(!m_pPlayer->IsEngineer() || !m_EngineerAction || m_BattlefieldRepairCooldown != 0)
		return;

	// Select the nearest occupied vehicle, not merely the nearest character.
	// Otherwise a closer infantry player prevents a friendly vehicle behind
	// them from ever reaching the repair branch.
	CCharacter *apCharacters[MAX_CLIENTS];
	int Num = GameWorld()->FindEntities(m_Pos, 300.0f,
		(CEntity **)apCharacters, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	CCharacter *pTarget = 0;
	float ClosestDistance = 300.0f;
	for(int i = 0; i < Num; i++)
	{
		CCharacter *pCandidate = apCharacters[i];
		if(!pCandidate || pCandidate == this || !pCandidate->IsAlive() ||
			!pCandidate->InBattlefieldVehicle() ||
			pCandidate->IsBattlefieldVehiclePassenger() ||
			pCandidate->GetBattlefieldVehicleType() == BATTLEFIELD_VEHICLE_MINI ||
			!pCandidate->GetPlayer())
			continue;
		float CandidateDistance = distance(m_Pos, pCandidate->m_Pos);
		if(!pTarget || CandidateDistance < ClosestDistance)
		{
			pTarget = pCandidate;
			ClosestDistance = CandidateDistance;
		}
	}

	if(pTarget)
	{
		if(GameServer()->Collision()->IntersectLine(m_Pos, pTarget->m_Pos, 0, 0))
		{
			if(m_EngineerObstructedCooldown == 0)
				m_EngineerObstructedCooldown = Server()->TickSpeed()*3;
			return;
		}

		if(pTarget->GetPlayer()->GetTeam() != m_pPlayer->GetTeam())
		{
			pTarget->TakeDamage(vec2(0, 0), 3, m_pPlayer->GetCID(), WEAPON_GUN);
			m_BattlefieldRepairCooldown = Server()->TickSpeed();
			return;
		}

		int Amount = 1;
		if(pTarget->m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_CAR)
			Amount = 2;
		else if(pTarget->m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER ||
			pTarget->m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_UBOAT ||
			pTarget->m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_SHIP)
			Amount = 5;

		int HealthBeforeRepair = pTarget->GetBattlefieldVehicleHealth();
		if(pTarget->RepairBattlefieldVehicle(Amount))
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Vehicle-Health: %i | %i", m_pPlayer->GetCID()),
				HealthBeforeRepair,
				CBattle::MaxHealth(pTarget->m_BattlefieldVehicleType));
			GameServer()->SendBroadcast(aBuf, m_pPlayer->GetCID());
			m_BroadcastClearTimer = 25;
			GameServer()->CreatePlayerSpawn(pTarget->m_Pos);
			m_BattlefieldRepairCooldown = Server()->TickSpeed();
		}
	}
}

bool CCharacter::HandleBattlefieldMedicAbilities()
{
	if(!m_pPlayer->IsMedic())
		return false;

	// These two timers are decremented inside the Medic branch, unlike the
	// independent support-projectile timers in HandleTicks().
	if(m_MedicRegenCooldown > 0)
		m_MedicRegenCooldown--;
	if(m_MedicDamageCooldown > 0)
		m_MedicDamageCooldown--;

	if(m_Health < 10 && m_MedicRegenCooldown == 0 && m_MedicDamageCooldown == 0)
	{
		IncreaseHealth(1);
		GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);
		m_MedicRegenCooldown = Server()->TickSpeed();
	}

	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_Pos, 500.0f, 0);
	if(!pTarget || !pTarget->IsAlive() || m_MedicHealCooldown != 0 ||
		!pTarget->GetPlayer() || pTarget->GetHealth() >= 10 ||
		pTarget->GetPlayer()->GetTeam() != m_pPlayer->GetTeam())
		return false;
	if(GameServer()->Collision()->IntersectLine(m_Pos, pTarget->m_Pos, 0, 0))
		return true;

	new CHealthShot(GameWorld(), m_Pos, SafeNormalize(pTarget->m_Pos-m_Pos),
		m_pPlayer->GetCID(), 2);
	m_MedicHealCooldown = round_to_int(Server()->TickSpeed()*0.5f);

	CCharacter *pSecond = GameWorld()->ClosestCharacter(m_Pos, 500.0f, pTarget);
	if(!pSecond || !pSecond->IsAlive() || m_MedicHealSecondCooldown != 0 ||
		!pSecond->GetPlayer() || pSecond->GetHealth() >= 10)
		return false;
	if(GameServer()->Collision()->IntersectLine(m_Pos, pSecond->m_Pos, 0, 0))
		return true;

	// The second-target branch reuses the already validated first
	// target's team result and therefore does not perform another team test.
	new CHealthShot(GameWorld(), m_Pos, SafeNormalize(pSecond->m_Pos-m_Pos),
		m_pPlayer->GetCID(), 2);
	m_MedicHealSecondCooldown = round_to_int(Server()->TickSpeed()*0.5f);
	return false;
}

bool CCharacter::RepairBattlefieldVehicle(int Amount)
{
	if(!InBattlefieldVehicle() || m_BattlefieldVehiclePassenger || Amount <= 0)
		return false;
	int Max = CBattle::MaxHealth(m_BattlefieldVehicleType);
	if(m_BattlefieldVehicleHealth >= Max)
		return false;
	m_BattlefieldVehicleHealth += Amount;
	return true;
}

void CCharacter::CreateVehicleDestructionEffects(int Type, vec2 Pos)
{
	if(Type != BATTLEFIELD_VEHICLE_HELI && Type != BATTLEFIELD_VEHICLE_JET &&
		Type != BATTLEFIELD_VEHICLE_PANZER && Type != BATTLEFIELD_VEHICLE_CAR)
		return;

	vec2 aOffsets[4] = {vec2(-50, 0), vec2(50, 0), vec2(50, -70), vec2(-50, -70)};
	int Num = Type == BATTLEFIELD_VEHICLE_CAR ? 2 : 4;
	for(int i = 0; i < Num; i++)
	{
		vec2 EffectPos = Pos+aOffsets[i];
		GameServer()->CreateExplosion(EffectPos, m_pPlayer->GetCID(), WEAPON_WORLD, false);
		GameServer()->CreateDeath(EffectPos, m_pPlayer->GetCID());
	}
	GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
}

bool CCharacter::CanEnterBattlefieldVehicle() const
{
	if(!m_pPlayer || !m_Alive)
		return false;
	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
		return false;
	if(InBattlefieldVehicle())
		return false;
	// CBattle::Tick requires the vehicle entry cooldown to be zero first.
	return m_BattlefieldVehicleEntryCooldown == 0;
}

bool CCharacter::EnterBattlefieldVehicle(CBattle *pVehicle, int Type, int Health)
{
	if(!pVehicle || !CanEnterBattlefieldVehicle())
		return false;
	if(Type != BATTLEFIELD_VEHICLE_HELI && Type != BATTLEFIELD_VEHICLE_JET &&
		Type != BATTLEFIELD_VEHICLE_PANZER && Type != BATTLEFIELD_VEHICLE_CAR &&
		Type != BATTLEFIELD_VEHICLE_UBOAT && Type != BATTLEFIELD_VEHICLE_SHIP &&
		Type != BATTLEFIELD_VEHICLE_MINI)
		return false;

	m_pBattlefieldVehicle = pVehicle;
	m_pBattlefieldSeatDriver = 0;
	m_pBattlefieldPassenger = 0;
	m_BattlefieldVehiclePassenger = false;
	m_BattlefieldVehicleType = Type;
	m_BattlefieldVehicleHealth = clamp(Health, 0, CBattle::MaxHealth(Type));
	m_BattlefieldVehicleWeaponCooldown = 0;
	m_BattlefieldVehicleAltCooldown = 0;
	m_BattlefieldVehicleFireToggle = false;
	m_BattlefieldHeliBombActive = false;
	m_BattlefieldVehicleSpeed = 0;
	m_BattlefieldVehicleAmmo = 10;
	m_BattlefieldVehicleBurstShots = 0;
	m_BattlefieldVehicleAmmoRegen = 0;
	m_BattlefieldVehicleOverheat = 0;
	m_BattlefieldVehicleMoveSound = Type == BATTLEFIELD_VEHICLE_JET ?
		Server()->TickSpeed()*5 : 0;
	m_BattlefieldVehicleGrace = Type == BATTLEFIELD_VEHICLE_JET ? Server()->TickSpeed()*5 : 0;
	m_HeliCollisionGrace = 0;
	m_HeliPassengerEntryDelay = 0;
	m_HeliBombCooldown = 0;
	m_JetFeedbackCooldown = 0;
	m_BattlefieldVehicleLandingCounter = 0;
	m_BattlefieldVehicleAirborne = false;
	m_BattlefieldVehicleSafeLanding = false;
	m_BattlefieldVehicleLanding = false;
	m_BattlefieldVehicleExitRequested = false;
	m_BattlefieldVehicleWaterResetPending = false;
	// require scoreboard release+press to exit (avoids sticky Shift / flag pulse)
	m_BattlefieldVehicleScoreboardHeld = true;
	m_BattlefieldUboatOutsideWater = false;
	m_BattlefieldUboatCollisionLatched = false;
	m_BattlefieldVehicleEntryCooldown = 0;
	m_Core.m_Vel = vec2(0, 0);
	m_Core.m_HookState = HOOK_RETRACTED;
	m_Core.m_HookedPlayer = -1;
	m_Core.m_TriggeredEvents = 0;
	GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_VEHICLE,
		"Vehicle tip: press Shift or type /e to exit.");
	return true;
}

bool CCharacter::EnterBattlefieldPassenger(CCharacter *pDriver)
{
	if(!pDriver || pDriver == this || !CanEnterBattlefieldVehicle() ||
		!pDriver->InBattlefieldVehicle() || pDriver->m_BattlefieldVehiclePassenger ||
		pDriver->m_pBattlefieldPassenger || !pDriver->m_pBattlefieldVehicle ||
		!pDriver->GetPlayer() || pDriver->GetPlayer()->GetTeam() != m_pPlayer->GetTeam())
		return false;
	if(pDriver->m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_HELI &&
		pDriver->m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_PANZER)
		return false;

	m_pBattlefieldVehicle = pDriver->m_pBattlefieldVehicle;
	m_pBattlefieldSeatDriver = pDriver;
	m_pBattlefieldPassenger = 0;
	m_BattlefieldVehiclePassenger = true;
	m_BattlefieldVehicleType = pDriver->m_BattlefieldVehicleType;
	m_BattlefieldVehicleHealth = pDriver->m_BattlefieldVehicleHealth;
	m_BattlefieldVehicleWeaponCooldown = 0;
	m_BattlefieldVehicleAltCooldown = 0;
	m_BattlefieldVehicleFireToggle = false;
	m_BattlefieldHeliBombActive = false;
	m_BattlefieldVehicleBurstShots = 0;
	m_BattlefieldVehicleEntryCooldown = 0;
	m_BattlefieldVehicleScoreboardHeld = true;
	m_Core.m_HookState = HOOK_RETRACTED;
	m_Core.m_HookedPlayer = -1;
	m_BattlefieldVehicleGrace = Server()->TickSpeed();
	m_HeliCollisionGrace = m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI ?
		Server()->TickSpeed() : 0;
	m_HeliPassengerEntryDelay = m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI ? 10 : 0;
	pDriver->m_pBattlefieldPassenger = this;
	pDriver->m_BattlefieldVehicleGrace = Server()->TickSpeed();
	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI)
		pDriver->m_HeliCollisionGrace = Server()->TickSpeed();
	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI)
		pDriver->m_aWeapons[WEAPON_SHOTGUN].m_Ammo = 10;
	else
		m_aWeapons[WEAPON_GUN].m_Ammo = 10;
	GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_VEHICLE,
		"Vehicle tip: press Shift or type /e to exit.");
	return true;
}

void CCharacter::RequestBattlefieldVehicleExit()
{
	if(m_InAntiAircraftCannon)
	{
		LeaveAntiAircraftCannon(true);
		return;
	}
	if(m_InAntiTankCannon)
	{
		LeaveAntiTankCannon(true);
		return;
	}
	if(!InBattlefieldVehicle())
		return;
	m_BattlefieldVehicleExitRequested = true;
}

void CCharacter::DetachBattlefieldVehicle(CBattle *pVehicle)
{
	if(pVehicle && pVehicle != m_pBattlefieldVehicle)
		return;
	if(m_BattlefieldVehiclePassenger && m_pBattlefieldSeatDriver &&
		m_pBattlefieldSeatDriver->m_pBattlefieldPassenger == this)
		m_pBattlefieldSeatDriver->m_pBattlefieldPassenger = 0;
	if(!m_BattlefieldVehiclePassenger && m_pBattlefieldPassenger)
	{
		CCharacter *pPassenger = m_pBattlefieldPassenger;
		m_pBattlefieldPassenger = 0;
		pPassenger->m_pBattlefieldSeatDriver = 0;
		pPassenger->DetachBattlefieldVehicle(0);
	}
	m_pBattlefieldVehicle = 0;
	m_pBattlefieldSeatDriver = 0;
	m_pBattlefieldPassenger = 0;
	m_BattlefieldVehiclePassenger = false;
	m_BattlefieldVehicleType = BATTLEFIELD_VEHICLE_NONE;
	m_BattlefieldVehicleHealth = 0;
	m_Armor = 0;
	m_BattlefieldVehicleWeaponCooldown = 0;
	m_BattlefieldVehicleAltCooldown = 0;
	m_BattlefieldHeliBombActive = false;
	m_BattlefieldVehicleSpeed = 0;
	m_BattlefieldVehicleAmmo = 10;
	m_BattlefieldVehicleBurstShots = 0;
	m_BattlefieldVehicleAmmoRegen = 0;
	m_BattlefieldVehicleOverheat = 0;
	m_BattlefieldVehicleMoveSound = 0;
	m_BattlefieldVehicleGrace = 0;
	m_HeliCollisionGrace = 0;
	m_HeliPassengerEntryDelay = 0;
	m_HeliBombCooldown = 0;
	m_JetFeedbackCooldown = 0;
	m_BattlefieldVehicleLandingCounter = 0;
	m_BattlefieldVehicleAirborne = false;
	m_BattlefieldVehicleSafeLanding = false;
	m_BattlefieldVehicleLanding = false;
	m_BattlefieldVehicleExitRequested = false;
	m_BattlefieldVehicleWaterResetPending = false;
	m_BattlefieldVehicleScoreboardHeld = false;
	m_BattlefieldUboatOutsideWater = false;
	m_BattlefieldUboatCollisionLatched = false;
	// One-second vehicle entry cooldown. Abandoned vehicles stay immediately
	// reclaimable; the spawn-return path uses a separate three-second lock.
	m_BattlefieldVehicleEntryCooldown = Server()->TickSpeed();
}

vec2 CCharacter::GetBattlefieldVehicleAim() const
{
	return SafeNormalize(vec2((float)m_LatestInput.m_TargetX,
		(float)m_LatestInput.m_TargetY));
}

vec2 CCharacter::GetSmokeLauncherAim() const
{
	return normalize(vec2((float)m_LatestInput.m_TargetX,
		(float)m_LatestInput.m_TargetY));
}

bool CCharacter::ClaimSmokeLauncher()
{
	if(m_HasSmokeLauncher)
		return false;
	m_HasSmokeLauncher = true;
	m_SmokeLauncherFireRequested = false;
	return true;
}

void CCharacter::LeaveBattlefieldVehicle(bool Destroyed, bool ImmediateReset)
{
	if(m_BattlefieldVehiclePassenger)
	{
		DetachBattlefieldVehicle(0);
		return;
	}
	CBattle *pVehicle = m_pBattlefieldVehicle;
	if(!pVehicle)
	{
		m_BattlefieldVehicleType = BATTLEFIELD_VEHICLE_NONE;
		m_BattlefieldVehicleHealth = 0;
		return;
	}

	// Destruction explosions deal real radial damage. If they are emitted while
	// this character still owns the zero-health vehicle, the first explosion
	// re-enters TakeDamage -> LeaveBattlefieldVehicle(true) and recursively
	// emits the same effect until the stack overflows. Preserve the visual/data
	// inputs, detach first, and only then publish the damaging explosions.
	const int VehicleType = m_BattlefieldVehicleType;
	const vec2 VehiclePos = m_Pos;
	pVehicle->OnDriverLeft(this, Destroyed, ImmediateReset);
	DetachBattlefieldVehicle(pVehicle);
	if(Destroyed)
		CreateVehicleDestructionEffects(VehicleType, VehiclePos);
}

void CCharacter::ResetBattlefieldVehicleFromWater()
{
	if(!InBattlefieldVehicle() || !m_pBattlefieldVehicle)
		return;
	// Shared by water, /e and Stop-triggered vehicle resets. CBattle observes
	// the released driver through its ordinary ten-second abandoned path.
	m_pBattlefieldVehicle->OnDriverLeft(this, false, false);
	DetachBattlefieldVehicle(m_pBattlefieldVehicle);
}

void CCharacter::PrepareBattlefieldVehicleWeaponState()
{
	int ForcedWeapon = -1;
	bool RefillOnEmpty = false;

	if(m_BattlefieldVehiclePassenger)
	{
		if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI ||
			m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER)
		{
			ForcedWeapon = WEAPON_GUN;
			RefillOnEmpty = true;
		}
	}
	else
	{
		switch(m_BattlefieldVehicleType)
		{
		case BATTLEFIELD_VEHICLE_HELI:
			ForcedWeapon = m_pBattlefieldPassenger ? WEAPON_SHOTGUN : WEAPON_GUN;
			RefillOnEmpty = true;
			break;
		case BATTLEFIELD_VEHICLE_JET: ForcedWeapon = WEAPON_NINJA; break;
		case BATTLEFIELD_VEHICLE_PANZER: ForcedWeapon = WEAPON_GRENADE; break;
		case BATTLEFIELD_VEHICLE_UBOAT: ForcedWeapon = WEAPON_GRENADE; break;
		case BATTLEFIELD_VEHICLE_SHIP:
			ForcedWeapon = WEAPON_GUN;
			RefillOnEmpty = true;
			break;
		case BATTLEFIELD_VEHICLE_MINI: ForcedWeapon = WEAPON_HAMMER; break;
		}
	}

	if(ForcedWeapon < 0)
		return;
	m_ActiveWeapon = ForcedWeapon;

	if(RefillOnEmpty && m_aWeapons[ForcedWeapon].m_Ammo == 0)
	{
		m_aWeapons[ForcedWeapon].m_Ammo = 10;
		m_ReloadTimer = Server()->TickSpeed()*5;
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
	}
}

void CCharacter::HandleBattlefieldPassenger()
{
	m_ProximityRadius = 28.0f;
	// Consume the passenger's shared vehicle-reset flag before follow/collision
	// work and clear only the seat relationship. The parent stays occupied.
	if(m_BattlefieldVehicleWaterResetPending)
	{
		DetachBattlefieldVehicle(0);
		return;
	}

	CCharacter *pDriver = m_pBattlefieldSeatDriver;
	if(m_BattlefieldVehiclePassenger && (!pDriver || !pDriver->IsAlive()))
	{
		DetachBattlefieldVehicle(0);
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		return;
	}
	if(!m_BattlefieldVehiclePassenger ||
		!pDriver->InBattlefieldVehicle() || pDriver->m_BattlefieldVehiclePassenger ||
		pDriver->m_pBattlefieldPassenger != this ||
		pDriver->m_BattlefieldVehicleType != m_BattlefieldVehicleType)
	{
		DetachBattlefieldVehicle(0);
		return;
	}
	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI &&
		(GameServer()->Collision()->isNoFlag(round_to_int(m_Pos.x), round_to_int(m_Pos.y)-50) ||
		 GameServer()->Collision()->isNoFlag(round_to_int(m_Pos.x)-50, round_to_int(m_Pos.y)) ||
		 GameServer()->Collision()->isNoFlag(round_to_int(m_Pos.x)+50, round_to_int(m_Pos.y))))
	{
		DetachBattlefieldVehicle(0);
		return;
	}

	m_BattlefieldVehicleHealth = pDriver->m_BattlefieldVehicleHealth;
	m_Pos = pDriver->m_Pos+vec2(0,
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI ? 40.0f : -40.0f);
	m_Core.m_Pos = m_Pos;
	m_Core.m_Vel = pDriver->m_Core.m_Vel;
	m_Core.m_Direction = 0;
	// Heli collision grace and the separate ten-tick entry delay must both
	// expire before the side-collision kill becomes active.
	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI &&
		m_HeliCollisionGrace > 0)
		m_HeliCollisionGrace--;
	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI &&
		m_HeliCollisionGrace == 0 && m_HeliPassengerEntryDelay == 0 &&
		(GameServer()->Collision()->CheckPoint(m_Pos.x+30.0f, m_Pos.y) ||
		 GameServer()->Collision()->CheckPoint(m_Pos.x-30.0f, m_Pos.y)))
	{
		pDriver->Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		return;
	}

	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI)
		TickHeliGunner();
	else if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER)
		TickPanzerGunner();
	RetractBattlefieldVehicleHook();
}

void CCharacter::RetractBattlefieldVehicleHook(bool ClearInputHook)
{
	// Most vehicle handlers consume hook, but Ship() and submerged Mini()
	// leave the persistent character input untouched. Core input is the copy
	// sampled before Core::Tick and is not rewritten by vehicle handlers.
	if(ClearInputHook)
		m_Input.m_Hook = 0;
	m_Core.m_HookState = HOOK_RETRACTED;
	m_Core.m_HookedPlayer = -1;
	m_Core.m_HookPos = m_Core.m_Pos;
	m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
}

void CCharacter::SendBattlefieldExtraProjectile(CProjectile *pProjectile)
{
	if(!pProjectile || !m_pPlayer)
		return;
	CNetObj_Projectile Projectile;
	pProjectile->FillInfo(&Projectile);
	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(1);
	for(unsigned i = 0; i < sizeof(Projectile)/sizeof(int); i++)
		Msg.AddInt(reinterpret_cast<int *>(&Projectile)[i]);
	Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
}

void CCharacter::FireBattlefieldVehicleWeapon(vec2 Direction, bool Alternate)
{
	if(!InBattlefieldVehicle() ||
		(m_BattlefieldVehicleWeaponCooldown > 0 &&
		 m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_HELI &&
		 !(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_UBOAT && Alternate)))
		return;

	Direction = SafeNormalize(Direction);

	switch(m_BattlefieldVehicleType)
	{
	case BATTLEFIELD_VEHICLE_HELI:
		if(m_HeliBombCooldown > 0 || m_BattlefieldHeliBombActive)
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			break;
		}
		new CBomb(GameWorld(), m_Pos, m_pPlayer->GetCID(), Direction*14.0f,
			CBomb::TYPE_HELI_BOMB, 0);
		m_BattlefieldHeliBombActive = true;
		m_HeliBombCooldown = Server()->TickSpeed()*8;
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		break;
	case BATTLEFIELD_VEHICLE_JET:
	{
		CProjectile *pProjectile = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
			m_pPlayer->GetCID(), m_Pos, Direction*2.0f,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
			1, false, 1, 0.0f, -1, WEAPON_SHOTGUN);
		SendBattlefieldExtraProjectile(pProjectile);
		m_BattlefieldVehicleWeaponCooldown = 8;
		GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		break;
	}
	case BATTLEFIELD_VEHICLE_PANZER:
	{
		CProjectile *pProjectile = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
			m_pPlayer->GetCID(), m_Pos, Direction*2.0f,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
			1, false, 1, 0.0f, -1, WEAPON_SHOTGUN);
		SendBattlefieldExtraProjectile(pProjectile);
		m_BattlefieldVehicleWeaponCooldown = 10;
		GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
		break;
	}
	case BATTLEFIELD_VEHICLE_CAR:
	{
		if(m_BattlefieldVehicleOverheat > 0 || m_BattlefieldVehicleAmmo <= 0)
			break;
		vec2 ShotDirection = (m_Core.m_Vel*0.015f+Direction)*0.5f;
		CProjectile *pProjectile = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
			m_pPlayer->GetCID(), m_Pos, ShotDirection,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
			1, true, 1, 0.0f, -1, WEAPON_SHOTGUN);
		SendBattlefieldExtraProjectile(pProjectile);
		m_BattlefieldVehicleWeaponCooldown = 35;
		m_BattlefieldVehicleAmmo--;
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		break;
	}
	case BATTLEFIELD_VEHICLE_UBOAT:
		if(Alternate)
		{
			new CBomb(GameWorld(), m_Pos, m_pPlayer->GetCID(), Direction,
				CBomb::TYPE_UBOAT_TORPEDO, 1);
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		}
		break;
	case BATTLEFIELD_VEHICLE_SHIP:
		new CBomb(GameWorld(), m_Pos+vec2(0, 20), m_pPlayer->GetCID(), Direction,
			CBomb::TYPE_SHIP_SHELL, 1);
		m_BattlefieldVehicleWeaponCooldown = 40;
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		break;
	case BATTLEFIELD_VEHICLE_MINI:
		// Mini() is a thrust action, not a projectile weapon.
		break;
	}
}

void CCharacter::ResetBattlefieldUboatShot()
{
	m_BattlefieldVehicleFireToggle = false;
	// Custom type 7 clears the fire toggle and sets the ordinary reload timer.
	// Do not stall the independent U-boat movement-feedback cadence here.
	m_ReloadTimer = max(m_ReloadTimer,
		Server()->TickSpeed()*2);
}

void CCharacter::FireAirstrike(vec2 Direction)
{
	if(m_AirstrikeShotCooldown > 0 || !m_pPlayer)
		return;
	Direction = SafeNormalize(Direction);
	if(Direction.y < -0.5f)
		Direction = SafeNormalize(vec2(Direction.x, 1.0f));

	CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
		m_pPlayer->GetCID(), m_Pos, Direction,
		(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
		1, true, 8, 0.0f, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);
	CNetObj_Projectile Projectile;
	pProj->FillInfo(&Projectile);
	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(1);
	for(unsigned i = 0; i < sizeof(Projectile)/sizeof(int); i++)
		Msg.AddInt(reinterpret_cast<int *>(&Projectile)[i]);
	Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
	m_AirstrikeShotCooldown = max(1, round_to_int(Server()->TickSpeed()*0.1f));
}

void CCharacter::Ship(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary)
{
	if(m_BattlefieldVehicleWeaponCooldown > 0)
		m_BattlefieldVehicleWeaponCooldown--;
	MaxSpeed = 30.0f;
	if(IsGrounded())
		Vel.x = 0.0f;
	if(m_InBattlefieldWater)
	{
		Vel.x += (float)m_Input.m_Direction*2.0f;
		Vel.y -= 0.8f;
	}
	FirePrimary = m_Input.m_Jump != 0;
}

void CCharacter::Mini(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed)
{
	MaxSpeed = 0.0f;
	m_ActiveWeapon = WEAPON_HAMMER;
	if(IsGrounded())
		Vel.x = 0.0f;
	if(IsGrounded() && !m_InBattlefieldWater)
	{
		LeaveBattlefieldVehicle(false);
		return;
	}
	if(m_Input.m_Hook != 0 && m_InBattlefieldWater)
	{
		Vel += Aim*2.3f;
		if(m_BattlefieldVehicleWeaponCooldown == 0)
		{
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP);
			GameServer()->CreateDamageInd(m_Pos+SafeNormalize(Vel)*10.0f,
				GetAngle(Vel)+2.0f, 1);
			m_BattlefieldVehicleWeaponCooldown = 2;
		}
	}
	// Mini() tests the shared vehicle cadence before decrementing it.
	if(m_BattlefieldVehicleWeaponCooldown > 0)
		m_BattlefieldVehicleWeaponCooldown--;
}

void CCharacter::Uboat(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary, bool &FireSecondary)
{
	MaxSpeed = 12.0f;
	if(GameServer()->Collision()->CheckPoint(Pos.x, Pos.y+30.0f) &&
		!m_InBattlefieldWater)
	{
		LeaveBattlefieldVehicle(false);
		return;
	}

	bool WaterAtCenter = GameServer()->Collision()->isWater(round_to_int(Pos.x), round_to_int(Pos.y));
	if(!WaterAtCenter)
		m_BattlefieldUboatOutsideWater = true;
	else
	{
		if(m_BattlefieldUboatOutsideWater && m_BattlefieldVehicleMoveSound == 0)
			Vel.y = 0.0f;
		m_BattlefieldUboatOutsideWater = false;
	}

	if(m_InBattlefieldWater)
	{
		Vel.y -= 0.4f;
		if(m_Input.m_Hook != 0)
		{
			if(Aim.y < 0.0f)
				Vel.y -= 2.0f;
			else if(Aim.y > 0.0f)
				Vel.y += 2.0f;
		}
		else
		{
			if(Vel.y > 0.0f)
				Vel.y -= 0.1f;
			if(Vel.y < 0.0f)
				Vel.y += 0.1f;
		}

		if(m_BattlefieldVehicleMoveSound > 0)
			m_BattlefieldVehicleMoveSound--;
		if(m_BattlefieldVehicleWeaponCooldown > 0)
			m_BattlefieldVehicleWeaponCooldown--;

		bool TouchingSolid =
			GameServer()->Collision()->CheckPoint(Pos.x+30.0f, Pos.y) ||
			GameServer()->Collision()->CheckPoint(Pos.x-30.0f, Pos.y) ||
			GameServer()->Collision()->CheckPoint(Pos.x, Pos.y-30.0f) ||
			GameServer()->Collision()->CheckPoint(Pos.x, Pos.y+30.0f);
		if(!TouchingSolid)
			m_BattlefieldUboatCollisionLatched = false;
		else if(!m_BattlefieldUboatCollisionLatched)
		{
			m_BattlefieldUboatCollisionLatched = true;
			GameServer()->CreateExplosion(Pos, m_pPlayer->GetCID(), WEAPON_WORLD, false);
			GameServer()->CreateSound(Pos, SOUND_HOOK_NOATTACH);
		}

		if(IsGrounded())
			Vel.x = 0.0f;
		if((m_Input.m_Direction != 0 || m_Input.m_Hook != 0) &&
			m_BattlefieldVehicleWeaponCooldown == 0)
		{
			GameServer()->CreateSound(Pos, SOUND_PLAYER_JUMP);
			GameServer()->CreateDamageInd(Pos+SafeNormalize(Vel)*-70.0f,
				GetAngle(Vel)+2.0f, 1);
			m_BattlefieldVehicleWeaponCooldown = 3;
		}

		Vel.x += (float)m_Input.m_Direction;
		Vel.x = clamp(Vel.x, -12.0f, 12.0f);
		Vel.y = clamp(Vel.y, -8.0f, 8.0f);
	}
	else if(m_BattlefieldOxygenActive)
	{
		Vel.y -= 0.2f;
		if(Vel.y < 0.0f)
			Vel.y += 0.2f;
	}
	else
		Vel.y += 0.6f;

	if(m_BattlefieldVehicleAltCooldown > 0)
		m_BattlefieldVehicleAltCooldown--;
	if(m_Input.m_Jump && m_BattlefieldVehicleAltCooldown == 0)
		m_BattlefieldVehicleAltCooldown = 150;
	FirePrimary = (m_Input.m_Fire&1) != 0;
	FireSecondary = m_BattlefieldVehicleAltCooldown == 150 ||
		m_BattlefieldVehicleAltCooldown == 135 ||
		m_BattlefieldVehicleAltCooldown == 120 ||
		m_BattlefieldVehicleAltCooldown == 105;
}

void CCharacter::Car(vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary)
{
	if(m_BattlefieldVehicleMoveSound > 0)
		m_BattlefieldVehicleMoveSound--;
	if(m_BattlefieldVehicleAltCooldown > 0)
		m_BattlefieldVehicleAltCooldown--;
	if(m_BattlefieldVehicleWeaponCooldown > 0)
		m_BattlefieldVehicleWeaponCooldown--;
	if(m_BattlefieldVehicleAmmoRegen > 0)
		m_BattlefieldVehicleAmmoRegen--;
	if(m_BattlefieldVehicleOverheat > 0)
		m_BattlefieldVehicleOverheat--;
	MaxSpeed = 30.0f;
	if(m_BattlefieldVehicleAmmo < 10 && m_BattlefieldVehicleAmmoRegen == 0)
	{
		m_BattlefieldVehicleAmmo++;
		m_BattlefieldVehicleAmmoRegen = round_to_int(Server()->TickSpeed()*0.9f);
	}
	if(m_BattlefieldVehicleAmmo == 0)
	{
		m_BattlefieldVehicleOverheat = round_to_int(Server()->TickSpeed()*6.5f);
		m_BattlefieldVehicleAmmo = 10;
	}

	Vel.x += (float)m_Input.m_Direction;
	Vel.x = clamp(Vel.x, -MaxSpeed, MaxSpeed);
	if(Vel.y < 0.0f)
		Vel.y = 0.0f;
	if(m_Input.m_Direction != 0 && m_BattlefieldVehicleMoveSound == 0)
	{
		GameServer()->CreateSound(m_Pos, SOUND_HOOK_LOOP);
		m_BattlefieldVehicleMoveSound = 10;
	}

	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_CAR ||
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER)
	{
		if(m_Input.m_Jump && m_BattlefieldVehicleAltCooldown == 0 &&
			(GameServer()->Collision()->CheckPoint(Pos.x+30.0f, Pos.y) ||
			 GameServer()->Collision()->CheckPoint(Pos.x-30.0f, Pos.y)))
		{
			Vel.y = -50.0f;
			m_BattlefieldVehicleAltCooldown = round_to_int(Server()->TickSpeed() *
				(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_CAR ? 0.3f : 0.4f));
		}
		if(GameServer()->Collision()->isNogo1(round_to_int(Pos.x), round_to_int(Pos.y)))
		{
			Vel.y = GameServer()->Collision()->isNogo1(round_to_int(Pos.x), round_to_int(Pos.y)-18) ? -5.0f : 0.0f;
		}
	}
	m_Input.m_Jump = 0;

	FirePrimary = m_Input.m_Hook != 0;
}

void CCharacter::JetFly(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary, bool &UnsafeJetLanding)
{
	FirePrimary = false;
	UnsafeJetLanding = false;
	Aim = SafeNormalize(Aim);

	if(m_AirstrikeShotCooldown > 0)
		m_AirstrikeShotCooldown--;

	if(m_AirstrikeBurstTimer > 0)
		m_AirstrikeBurstTimer--;

	if(m_AirstrikeCooldown > 0)
		m_AirstrikeCooldown--;

	if(m_AirstrikeCooldown == 0 && (m_Input.m_Fire&1))
	{
		m_AirstrikeBurstTimer = Server()->TickSpeed();
		m_AirstrikeCooldown = Server()->TickSpeed()*5;
	}

	if(m_AirstrikeBurstTimer > 0)
		FireAirstrike(Aim);

	if(m_BattlefieldVehicleMoveSound > 0)
		m_BattlefieldVehicleMoveSound--;

	if(m_BattlefieldVehicleWeaponCooldown > 0)
		m_BattlefieldVehicleWeaponCooldown--;

	if(m_JetFeedbackCooldown > 0)
		m_JetFeedbackCooldown--;

	MaxSpeed = 0.0f;
	bool Grounded = IsGrounded();
	vec2 FeedbackPos = Pos+Aim*-70.0f;
	if(m_JetFeedbackCooldown < 1)
	{
		GameServer()->CreateDamageInd(FeedbackPos, GetAngle(Aim)+2.0f, 1);
		m_JetFeedbackCooldown = 30-m_BattlefieldVehicleSpeed;
		GameServer()->CreateSound(Pos, SOUND_BODY_LAND);
	}
	if(Grounded && m_Input.m_Direction != 0)
	{
		GameServer()->CreateSound(Pos, SOUND_BODY_LAND);
		GameServer()->CreateDamageInd(FeedbackPos, GetAngle(Aim)+2.0f, 1);
	}

	if(m_BattlefieldVehicleWaterResetPending)
	{
		m_Core.m_Vel = vec2(0.0f, -20.0f);
		ResetBattlefieldVehicleFromWater();
		return;
	}

	if(m_Input.m_Hook != 0 && m_BattlefieldVehicleWeaponCooldown == 0)
		FireBattlefieldVehicleWeapon(Aim, false);
	m_Input.m_Jump = 0;
	m_Input.m_Hook = 0;

	if((GameServer()->Collision()->CheckPoint(Pos.x+30.0f, Pos.y) ||
		GameServer()->Collision()->CheckPoint(Pos.x-30.0f, Pos.y) ||
		GameServer()->Collision()->CheckPoint(Pos.x, Pos.y-30.0f)) &&
		m_BattlefieldVehicleMoveSound == 0)
	{
		const vec2 aOffsets[4] = {
			vec2(-70.0f, 0.0f), vec2(70.0f, 0.0f),
			vec2(70.0f, -70.0f), vec2(-70.0f, -70.0f)};
		for(int i = 0; i < 4; i++)
			GameServer()->CreateExplosion(Pos+aOffsets[i],
				m_pPlayer->GetCID(), WEAPON_WORLD, false);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		for(int i = -30; i <= 30; i++)
		{
			float Angle = GetAngle(Aim)+i*0.1f;
			float v = 1.0f-absolute(i)/30.0f;
			float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff,
				1.0f, v);
			new CProjectile(GameWorld(), WEAPON_SHOTGUN, m_pPlayer->GetCID(), Pos,
				GetDir(Angle)*Speed, max(1, round_to_int(Server()->TickSpeed()*0.15f)),
				1, false, 1, 0.0f, -1, WEAPON_SHOTGUN);
		}
		LeaveBattlefieldVehicle(false, false);
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		return;
	}

	// Speed input is disabled while the previous tick's landing state is active.
	if(!m_BattlefieldVehicleLanding)
	{
		if(m_Input.m_Direction < 0 && m_BattlefieldVehicleSpeed > 0)
			m_BattlefieldVehicleSpeed--;
		else if(m_Input.m_Direction > 0 && m_BattlefieldVehicleSpeed < 60)
			m_BattlefieldVehicleSpeed++;
	}

	if(Grounded && !m_BattlefieldVehicleLanding)
		Vel.x += Aim.x*m_BattlefieldVehicleSpeed;
	else
		Vel.x += Aim.x*m_BattlefieldVehicleSpeed/15.0f;
	Vel.y += Aim.y*m_BattlefieldVehicleSpeed/20.0f;

	if(m_BattlefieldVehicleLanding)
	{
		Vel.y = 10.0f;
		Vel.x += Aim.x*m_BattlefieldVehicleSpeed/5.0f;
		if(m_BattlefieldVehicleSpeed == 0)
		{
			m_BattlefieldVehicleWaterResetPending = true;
			Vel.y = -20.0f;
		}
		m_BattlefieldVehicleLandingCounter++;
		if(m_BattlefieldVehicleLandingCounter >= 2)
		{
			m_BattlefieldVehicleLandingCounter = 0;
			if(m_BattlefieldVehicleSpeed > 0)
				m_BattlefieldVehicleSpeed--;
		}
	}
	Vel.y = clamp(Vel.y, -10.0f, 15.0f);
	m_Input.m_Direction = 0;

	// Landing safety is updated after movement and therefore controls the next
	// tick's acceleration branch, exactly as in JetFly.
	if(m_BattlefieldVehicleAirborne &&
		m_BattlefieldVehicleMoveSound < Server()->TickSpeed()*3)
	{
		if(absolute(Aim.x) > absolute(Aim.y))
			m_BattlefieldVehicleSafeLanding = true;
		else if(!m_BattlefieldVehicleLanding)
			m_BattlefieldVehicleSafeLanding = false;
	}
	if(!Grounded)
		m_BattlefieldVehicleAirborne = true;
	m_BattlefieldVehicleLanding = Grounded && m_BattlefieldVehicleSafeLanding;

	if(Grounded && m_BattlefieldVehicleAirborne &&
		m_BattlefieldVehicleMoveSound < 120 &&
		!m_BattlefieldVehicleSafeLanding)
	{
		const vec2 aOffsets[4] = {
			vec2(-70.0f, 0.0f), vec2(70.0f, 0.0f),
			vec2(70.0f, -70.0f), vec2(-70.0f, -70.0f)};
		for(int i = 0; i < 4; i++)
			GameServer()->CreateExplosion(Pos+aOffsets[i],
				m_pPlayer->GetCID(), WEAPON_WORLD, false);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		for(int i = -30; i <= 30; i++)
		{
			float Angle = GetAngle(Aim)+i*0.1f;
			float v = 1.0f-absolute(i)/30.0f;
			float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff,
				1.0f, v);
			new CProjectile(GameWorld(), WEAPON_SHOTGUN, m_pPlayer->GetCID(), Pos,
				GetDir(Angle)*Speed, max(1, round_to_int(Server()->TickSpeed()*0.15f)),
				1, false, 1, 0.0f, -1, WEAPON_SHOTGUN);
		}
		LeaveBattlefieldVehicle(false, false);
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Speed: %i | 60, Health: %i | 10", m_pPlayer->GetCID()),
		m_BattlefieldVehicleSpeed, m_BattlefieldVehicleHealth);
	GameServer()->SendBroadcast(aBuf, m_pPlayer->GetCID());
	m_BroadcastClearTimer = 3;
}

void CCharacter::HeliFly(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary)
{
	FirePrimary = false;
	if(m_BattlefieldVehicleWeaponCooldown > 0)
		m_BattlefieldVehicleWeaponCooldown--;
	if(m_HeliBombCooldown > 0)
		m_HeliBombCooldown--;
	MaxSpeed = 12.0f;
	if(IsGrounded() && m_Input.m_Hook == 0)
		Vel.y = 0.0f;

	// Weapon cooldown gates all of this block in HeliFly, including the Jump bomb.
	if(m_BattlefieldVehicleWeaponCooldown == 0)
	{
		if(Vel.y > 0.0f)
		{
			GameServer()->CreateDamageInd(Pos, 3.5f, 1);
			GameServer()->CreateSound(Pos, SOUND_WEAPON_SWITCH);
			m_BattlefieldVehicleWeaponCooldown =
				max(1, round_to_int(Server()->TickSpeed()*0.1f));
		}
		if(m_Input.m_Jump)
			FireBattlefieldVehicleWeapon(Aim, false);
		if(Vel.y < 0.0f)
		{
			GameServer()->CreateDamageInd(Pos, 0.5f, 1);
			GameServer()->CreateSound(Pos, SOUND_WEAPON_SWITCH);
			m_BattlefieldVehicleWeaponCooldown =
				max(1, round_to_int(Server()->TickSpeed()*0.1f));
		}
	}

	if((GameServer()->Collision()->CheckPoint(Pos.x+30.0f, Pos.y) ||
		GameServer()->Collision()->CheckPoint(Pos.x-30.0f, Pos.y) ||
		GameServer()->Collision()->CheckPoint(Pos.x, Pos.y-30.0f)) &&
		m_HeliCollisionGrace == 0)
	{
		const vec2 aOffsets[4] = {
			vec2(-70.0f, 0.0f), vec2(70.0f, 0.0f),
			vec2(70.0f, -70.0f), vec2(-70.0f, -70.0f)};
		for(int i = 0; i < 4; i++)
			GameServer()->CreateExplosion(Pos+aOffsets[i],
				m_pPlayer->GetCID(), WEAPON_WORLD, false);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateSound(Pos, SOUND_GRENADE_EXPLODE);

		vec2 CrashDirection = vec2(Aim.x < 0.0f ? -1.0f : 1.0f, 0.0f);
		for(int i = -30; i <= 30; i++)
		{
			float Angle = GetAngle(CrashDirection)+i*0.1f;
			float v = 1.0f-absolute(i)/30.0f;
			float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff,
				1.0f, v);
			new CProjectile(GameWorld(), WEAPON_SHOTGUN, m_pPlayer->GetCID(), Pos,
				GetDir(Angle)*Speed, max(1, round_to_int(Server()->TickSpeed()*0.15f)),
				1, false, 1, 0.0f, -1, WEAPON_SHOTGUN);
		}
		LeaveBattlefieldVehicle(false, false);
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		return;
	}

	// IsGrounded probes are at x +/- radius/2 and y + radius/2+5.
	if(GameServer()->Collision()->CheckPoint(Pos.x+15.0f, Pos.y+20.0f) ||
		GameServer()->Collision()->CheckPoint(Pos.x-15.0f, Pos.y+20.0f))
		m_Input.m_Direction = 0;
	if(m_HeliCollisionGrace > 0)
		m_HeliCollisionGrace--;

	if(m_pBattlefieldPassenger)
	{
		bool LowerContact = GameServer()->Collision()->CheckPoint(Pos.x, Pos.y+55.0f);
		if(m_HeliCollisionGrace > round_to_int(Server()->TickSpeed()*0.9f) &&
			GameServer()->Collision()->CheckPoint(Pos.x, Pos.y+20.0f))
			Pos.y -= 55.0f;
		if(m_HeliCollisionGrace > round_to_int(Server()->TickSpeed()*0.9f) && LowerContact)
			Pos.y -= 55.0f;
		if(LowerContact)
		{
			m_Input.m_Direction = 0;
			Vel.x = 0.0f;
			Vel.y = -0.5f;
		}
	}
	Vel.x += (float)m_Input.m_Direction;
	if(IsGrounded())
		Vel.x = 0.0f;
	if(m_Input.m_Hook != 0)
		Vel.y -= 5.0f;
	Vel.x = clamp(Vel.x, -12.0f, 12.0f);
	Vel.y = clamp(Vel.y, -8.0f, 4.0f);
}

void CCharacter::Panzer(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary)
{
	m_aWeapons[WEAPON_GRENADE].m_Ammo =
		min(m_aWeapons[WEAPON_GRENADE].m_Ammo+1, 10);
	if(m_BattlefieldVehicleMoveSound > 0)
		m_BattlefieldVehicleMoveSound--;
	if(m_BattlefieldVehicleAltCooldown > 0)
		m_BattlefieldVehicleAltCooldown--;
	if(m_BattlefieldVehicleWeaponCooldown > 0)
		m_BattlefieldVehicleWeaponCooldown--;
	MaxSpeed = 5.0f;
	if(Vel.y < 0.0f)
		Vel.y = 0.0f;
	if(m_Input.m_Direction != 0)
		Vel.x = (float)m_Input.m_Direction*5.0f;
	if(m_Input.m_Direction != 0 && m_BattlefieldVehicleMoveSound == 0)
	{
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		m_BattlefieldVehicleMoveSound = 5;
	}
	if(m_Input.m_Jump && m_BattlefieldVehicleAltCooldown == 0 &&
		(GameServer()->Collision()->CheckPoint(Pos.x+30.0f, Pos.y) ||
		 GameServer()->Collision()->CheckPoint(Pos.x-30.0f, Pos.y)))
	{
		Vel.y = -50.0f;
		m_BattlefieldVehicleAltCooldown =
			max(1, round_to_int(Server()->TickSpeed()*0.4f));
	}
	m_Input.m_Jump = 0;
	if(GameServer()->Collision()->isNogo1(round_to_int(Pos.x), round_to_int(Pos.y)))
		Vel.y = GameServer()->Collision()->isNogo1(
			round_to_int(Pos.x), round_to_int(Pos.y)-18) ? -5.0f : 0.0f;
	FirePrimary = m_Input.m_Hook != 0;
}

void CCharacter::TickHeliGunner()
{
}

void CCharacter::TickPanzerGunner()
{
	vec2 Direction = GetBattlefieldVehicleAim();
	if(m_Input.m_Hook != 0 && m_BattlefieldVehicleWeaponCooldown == 0)
	{
		CProjectile *pShell = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
			m_pPlayer->GetCID(), m_Pos, Direction*2.0f,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_ShotgunLifetime),
			1, false, 1, 0.0f, -1, WEAPON_SHOTGUN);
		SendBattlefieldExtraProjectile(pShell);
		m_BattlefieldVehicleWeaponCooldown = 7;
		GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
	}
}

void CCharacter::HandleBattlefieldVehicle()
{
	if(!InBattlefieldVehicle())
		return;

	if((m_Input.m_PlayerFlags&PLAYERFLAG_SCOREBOARD) != 0)
	{
		if(!m_BattlefieldVehicleScoreboardHeld)
		{
			RequestBattlefieldVehicleExit();
			m_BattlefieldVehicleScoreboardHeld = true;
		}
	}
	else
		m_BattlefieldVehicleScoreboardHeld = false;

	if(m_BattlefieldVehicleExitRequested)
	{
		LeaveBattlefieldVehicle(false);
		return;
	}
	PrepareBattlefieldVehicleWeaponState();
	if(m_BattlefieldVehiclePassenger)
	{
		HandleBattlefieldPassenger();
		return;
	}
	if(m_BattlefieldVehicleWaterResetPending &&
		m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_JET)
	{
		ResetBattlefieldVehicleFromWater();
		return;
	}

	switch(m_BattlefieldVehicleType)
	{
	case BATTLEFIELD_VEHICLE_JET:
		m_ProximityRadius = 70.0f;
		break;
	case BATTLEFIELD_VEHICLE_PANZER:
		m_ProximityRadius = m_pBattlefieldPassenger ? 28.0f : 70.0f;
		break;
	case BATTLEFIELD_VEHICLE_CAR:
	case BATTLEFIELD_VEHICLE_UBOAT:
	case BATTLEFIELD_VEHICLE_SHIP:
		m_ProximityRadius = 50.0f;
		break;
	default:
		m_ProximityRadius = 28.0f;
		break;
	}

	vec2 Aim = GetBattlefieldVehicleAim();
	vec2 Vel = m_Core.m_Vel;
	vec2 Pos = m_Pos;
	float MaxSpeed = 0.0f;
	bool FirePrimary = false;
	bool FireSecondary = false;
	bool UnsafeJetLanding = false;

	switch(m_BattlefieldVehicleType)
	{
	case BATTLEFIELD_VEHICLE_HELI:
		HeliFly(Aim, Vel, Pos, MaxSpeed, FirePrimary);
		break;
	case BATTLEFIELD_VEHICLE_JET:
		JetFly(Aim, Vel, Pos, MaxSpeed, FirePrimary, UnsafeJetLanding);
		break;
	case BATTLEFIELD_VEHICLE_PANZER:
		Panzer(Aim, Vel, Pos, MaxSpeed, FirePrimary);
		break;
	case BATTLEFIELD_VEHICLE_CAR:
		Car(Vel, Pos, MaxSpeed, FirePrimary);
		break;
	case BATTLEFIELD_VEHICLE_UBOAT:
		Uboat(Aim, Vel, Pos, MaxSpeed, FirePrimary, FireSecondary);
		if(!InBattlefieldVehicle())
			return;
		break;
	case BATTLEFIELD_VEHICLE_SHIP:
		Ship(Aim, Vel, Pos, MaxSpeed, FirePrimary);
		break;
	case BATTLEFIELD_VEHICLE_MINI:
		Mini(Aim, Vel, Pos, MaxSpeed);
		if(!InBattlefieldVehicle())
			return;
		break;
	}
	if(!IsAlive() || !InBattlefieldVehicle())
		return;

	if(m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_JET)
		m_BattlefieldVehicleSpeed = (int)round_to_int(length(Vel));
	m_Core.m_Pos = Pos;
	m_Core.m_Vel = Vel;
	m_Core.m_Direction = m_Input.m_Direction;

	if(FirePrimary && m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_UBOAT)
		FireBattlefieldVehicleWeapon(Aim, false);
	if(FireSecondary)
		FireBattlefieldVehicleWeapon(Aim, true);
	if(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_JET)
		m_Input.m_Direction = 0;
	const bool PreserveHookInput =
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_SHIP ||
		(m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_MINI &&
		 m_InBattlefieldWater);
	RetractBattlefieldVehicleHook(!PreserveHookInput);
}

void CCharacter::Tele(vec2 Pos)
{
	GameServer()->CreatePlayerSpawn(m_Pos);
	m_Core.m_Pos = Pos;
	m_Core.m_HookPos = Pos;
	m_Core.m_Direction = -1;
	m_Core.m_HookedPlayer = -1;
	m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
	GameServer()->CreatePlayerSpawn(Pos);
}

void CCharacter::CollideWithDoor(vec2 From, vec2 To)
{
	if(From.x == To.x)
	{
		// CDoor::HitCharacter calls Stop(270) and Stop(90), in that order.
		BlockBattlefieldTile(TILEFLAG_VFLIP|TILEFLAG_HFLIP|TILEFLAG_ROTATE, 0);
		BlockBattlefieldTile(TILEFLAG_ROTATE, 0);
		float Delta = m_Pos.x-From.x;
		// Infantry uses asymmetric body extents: 50 pixels on
		// the positive side and 28 on the negative side. Vehicles use 70/70.
		float PositivePushRange = InBattlefieldVehicle() ? 70.0f : 50.0f;
		float NegativePushRange = InBattlefieldVehicle() ? 70.0f : 28.0f;
		if(Delta > 0.0f && Delta < PositivePushRange)
			m_Core.m_Pos.x += 1.0f;
		else if(Delta <= 0.0f && Delta > -NegativePushRange)
			m_Core.m_Pos.x -= 1.0f;
	}
	if(From.y == To.y)
	{
		BlockBattlefieldTile(0, 0);
		BlockBattlefieldTile(TILEFLAG_VFLIP|TILEFLAG_HFLIP, 0);
		BlockBattlefieldTile(TILEFLAG_VFLIP|TILEFLAG_HFLIP|TILEFLAG_ROTATE, 0);
		BlockBattlefieldTile(TILEFLAG_ROTATE, 0);
	}
	if(From.x != To.x && From.y != To.y)
	{
		BlockBattlefieldTile(0, 0);
		BlockBattlefieldTile(TILEFLAG_VFLIP|TILEFLAG_HFLIP, 0);
		BlockBattlefieldTile(TILEFLAG_VFLIP|TILEFLAG_HFLIP|TILEFLAG_ROTATE, 0);
		BlockBattlefieldTile(TILEFLAG_ROTATE, 0);
	}
}

void CCharacter::Check(int Checkpoint)
{
	if(Checkpoint < 1 || Checkpoint > 3 || !m_pPlayer)
		return;

	int Index = Checkpoint-1;
	int &State = GameServer()->m_aCheckpointState[Index];
	int Gauge = clamp(State/80, -5, 5);
	static const char *s_apGauge[11] =
	{
		"|~~~~~~~~~~", "~|~~~~~~~~~", "~~|~~~~~~~~", "~~~|~~~~~~~",
		"~~~~|~~~~~~", "~~~~~|~~~~~", "~~~~~~|~~~~", "~~~~~~~|~~~",
		"~~~~~~~~|~~", "~~~~~~~~~|~", "~~~~~~~~~~|"
	};
	GameServer()->SendBroadcast(s_apGauge[Gauge+5], m_pPlayer->GetCID());
	m_BroadcastClearTimer = 20;
	GameServer()->TrySendTip(m_pPlayer->GetCID(), CPlayer::TIP_CHECKPOINT,
		"Checkpoint tip: stand here to capture A/B/C. Your team gets score; enemies get a warning chat.");

	if(State > -401 && State < 401)
	{
		if(m_pPlayer->GetTeam() == TEAM_RED)
		{
			State--;
			if(State > 0 && !GameServer()->m_aCheckpointWarning[Index][TEAM_RED] &&
				GameServer()->m_aCheckpointWarningCooldown[TEAM_RED] == 0)
			{
				char aBuf[256];
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!GameServer()->m_apPlayers[i] || GameServer()->m_apPlayers[i]->GetTeam() != TEAM_BLUE)
						continue;
					str_format(aBuf, sizeof(aBuf), GameServer()->Localize("[Warning] Red-Team is attacking Checkpoint %c!", i),
						'A'+Index);
					CNetMsg_Sv_Chat Msg;
					Msg.m_Team = 1;
					Msg.m_ClientID = -1;
					Msg.m_pMessage = aBuf;
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
				}
				GameServer()->m_aCheckpointWarning[Index][TEAM_RED] = true;
				GameServer()->m_aCheckpointWarningCooldown[TEAM_RED] = Server()->TickSpeed()*15;
			}
		}
		else if(m_pPlayer->GetTeam() == TEAM_BLUE)
		{
			State++;
			if(State < 0 && !GameServer()->m_aCheckpointWarning[Index][TEAM_BLUE] &&
				GameServer()->m_aCheckpointWarningCooldown[TEAM_BLUE] == 0)
			{
				char aBuf[256];
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!GameServer()->m_apPlayers[i] || GameServer()->m_apPlayers[i]->GetTeam() != TEAM_RED)
						continue;
					str_format(aBuf, sizeof(aBuf), GameServer()->Localize("[Warning] Blue-Team is attacking Checkpoint %c!", i),
						'A'+Index);
					CNetMsg_Sv_Chat Msg;
					Msg.m_Team = 1;
					Msg.m_ClientID = -1;
					Msg.m_pMessage = aBuf;
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
				}
				GameServer()->m_aCheckpointWarning[Index][TEAM_BLUE] = true;
				GameServer()->m_aCheckpointWarningCooldown[TEAM_BLUE] = Server()->TickSpeed()*15;
			}
		}
	}

	if(State == 0)
	{
		GameServer()->m_aCheckpointCaptured[Index] = false;
		GameServer()->m_aCheckpointWarning[Index][TEAM_RED] = false;
		GameServer()->m_aCheckpointWarning[Index][TEAM_BLUE] = false;
	}

	int CapturingTeam = -1;
	if(State < -400)
	{
		State = -400;
		// At Red's endpoint clear the Blue attack warning.
		GameServer()->m_aCheckpointWarning[Index][TEAM_BLUE] = false;
		CapturingTeam = TEAM_RED;
	}
	else if(State > 400)
	{
		State = 400;
		// At Blue's endpoint clear the Red attack warning.
		GameServer()->m_aCheckpointWarning[Index][TEAM_RED] = false;
		CapturingTeam = TEAM_BLUE;
	}

	if(CapturingTeam != -1 && !GameServer()->m_aCheckpointCaptured[Index])
	{
		GameServer()->m_aCheckpointCaptured[Index] = true;
		m_pPlayer->m_Score += 5;
		GameServer()->m_pController->AddTeamScore(CapturingTeam, 20);
		GameServer()->CreateSound(m_Pos, SOUND_CTF_CAPTURE);

		char aBuf[256];
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!GameServer()->m_apPlayers[i])
				continue;
			str_format(aBuf, sizeof(aBuf), GameServer()->Localize("%s-Team (%s) captured Checkpoint %c!", i),
				GameServer()->Localize(CapturingTeam == TEAM_RED ? "Red" : "Blue", i),
				Server()->ClientName(m_pPlayer->GetCID()), 'A'+Index);
			CNetMsg_Sv_Chat Msg;
			Msg.m_Team = 0;
			Msg.m_ClientID = -1;
			Msg.m_pMessage = aBuf;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}

	// Emit SOUND_HOOK_NOATTACH at every 80-point milestone,
	// including neutral, with a distinct 0.8-second gate for each checkpoint.
	if((State % 80) == 0 && GameServer()->m_aCheckpointProgressSoundCooldown[Index] == 0)
	{
		GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH);
		GameServer()->m_aCheckpointProgressSoundCooldown[Index] = (int)(Server()->TickSpeed()*0.8f + 0.5f);
	}
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if (m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// or are not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS &&
		!m_InAntiTankCannon)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	if(m_pPlayer->m_ForceBalanced)
	{
		char Buf[128];
		str_format(Buf, sizeof(Buf), GameServer()->Localize("You were moved to %s due to team balancing", m_pPlayer->GetCID()),
			GameServer()->Localize(GameServer()->m_pController->GetTeamName(m_pPlayer->GetTeam()), m_pPlayer->GetCID()));
		GameServer()->SendBroadcast(Buf, m_pPlayer->GetCID());

		m_pPlayer->m_ForceBalanced = false;
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);

	// Death-tile test runs before custom handlers. Die() removes the character
	// immediately, but the current Tick invocation deliberately continues.
	if(GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
		GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	HandleTicks();
	HandleTiles();
	HandleOther();

	// Mine/door entities consume the previous tick's engineer pulse before
	// characters are ticked. Clear it here; firing the gun below creates the
	// pulse observed by those entities on the next world tick.
	m_EngineerAction = false;
	if(!InBattlefieldVehicle())
		m_ProximityRadius = 28.0f;

	if(InBattlefieldVehicle())
	{
		HandleWeapons();
		HandleBattlefieldVehicle();
	}
	else if(m_InAntiTankCannon)
	{
		m_Core.m_Vel = vec2(0, 0);
		m_Core.m_Pos = m_AntiTankStationPos;
		m_Pos = m_AntiTankStationPos;
	}
	else if(m_InAntiAircraftCannon)
	{
		HandleWeapons();
		m_Core.m_Vel = vec2(0, 0);
		m_Core.m_Pos = m_AntiAircraftStationPos;
		m_Pos = m_AntiAircraftStationPos;
	}
	else
	{
		// handle Weapons
		HandleWeapons();
	}

	// HandleTiles on the next game tick samples the segment between this
	// pre-deferred core position and the position produced by TickDefered.
	m_BattlefieldTileLastPos = m_Core.m_Pos;

	// Previnput
	m_PrevInput = m_Input;
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move();
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());

	if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);

	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
	if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);


	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	if(!m_Alive)
		return;

	// A Heli/Panzer gunner used to keep its parent pointer until the next
	// gunner tick, so a dead parent also killed the gunner. Explicit detach
	// clears that pointer immediately, so retain the same
	// outcome here after the driver's own death has been published.
	CCharacter *pVehiclePassenger =
		!m_BattlefieldVehiclePassenger ? m_pBattlefieldPassenger : 0;

	m_pPlayer->StartRespawn();
	GameServer()->SendBroadcast("", m_pPlayer->GetCID());
	LeaveAntiTankCannon();
	LeaveAntiAircraftCannon();
	LeaveBattlefieldVehicle(false, true);

	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	CPlayer *pKillerPlayer = Killer >= 0 && Killer < MAX_CLIENTS ?
		GameServer()->m_apPlayers[Killer] : 0;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, pKillerPlayer, Weapon);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	if(pKillerPlayer)
	{
		CCharacter *pKiller = pKillerPlayer->GetCharacter();
		if(pKiller && pKiller->IsAlive())
			pKiller->RegisterBattlefieldKill(Killer != m_pPlayer->GetCID());
	}

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());

	if(pVehiclePassenger && pVehiclePassenger->IsAlive() &&
		pVehiclePassenger->GetPlayer())
		pVehiclePassenger->Die(pVehiclePassenger->GetPlayer()->GetCID(), WEAPON_WORLD);
}

void CCharacter::SendBattlefieldHitSound(int From, int Sound)
{
	if(From < 0 || From >= MAX_CLIENTS || From == m_pPlayer->GetCID() ||
		!GameServer()->m_apPlayers[From])
		return;

	int Mask = CmaskOne(From);
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] &&
			GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS &&
			GameServer()->m_apPlayers[i]->m_SpectatorID == From)
			Mask |= CmaskOne(i);
	}
	GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos,
		Sound, Mask);
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon)
{
	m_MedicDamageCooldown = Server()->TickSpeed()*5;
	if(m_GodModeTimer > 0)
		return false;

	if(!InBattlefieldVehicle())
		m_Core.m_Vel += Force;

	if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && !g_Config.m_SvTeamdamage)
		return false;

	bool ApplyDamage = Dmg != 0;
	if(From == m_pPlayer->GetCID())
	{
		Dmg = max(1, Dmg/2);
		ApplyDamage = true;
	}

	m_DamageTaken++;
	const int VehicleTypeBeforeDamage = m_BattlefieldVehicleType;
	const bool WasVehiclePassenger = m_BattlefieldVehiclePassenger;
	const bool DamageIndicatorVehicle =
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI ||
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_JET ||
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER ||
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_CAR;
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		if(DamageIndicatorVehicle)
			GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
		else
			GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
	}
	else
	{
		m_DamageTaken = 0;
		if(DamageIndicatorVehicle)
			GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
		else
			GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
	}

	if(m_BattlefieldVehiclePassenger && ApplyDamage)
	{
		CCharacter *pDriver = m_pBattlefieldSeatDriver;
		if(m_BattlefieldVehicleHealth != 0 && pDriver && pDriver->IsAlive())
		{
			pDriver->m_BattlefieldVehicleHealth -= Dmg;
			m_BattlefieldVehicleHealth = pDriver->m_BattlefieldVehicleHealth;
			if(pDriver->m_BattlefieldVehicleHealth < 1)
			{
				Die(From, Weapon);
				pDriver->LeaveBattlefieldVehicle(true);
				pDriver->Die(From, Weapon);
			}
			Dmg = 0;
		}
	}
	else if(InBattlefieldVehicle() &&
		m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_MINI && ApplyDamage)
	{
		if(Dmg != 0)
		{
			if(m_BattlefieldVehicleHealth != 0)
				m_BattlefieldVehicleHealth -= Dmg;
			if(m_BattlefieldVehicleHealth < 1)
			{
				LeaveBattlefieldVehicle(true);
				Die(From, Weapon);
			}
			Dmg = 0;
		}
	}
	else if(ApplyDamage)
		m_Health -= Dmg;

	m_DamageTakenTick = Server()->Tick();
	const bool HitSoundVehicle = WasVehiclePassenger ||
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_HELI ||
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_JET ||
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_PANZER ||
		m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_CAR;
	SendBattlefieldHitSound(From,
		HitSoundVehicle ? SOUND_HOOK_NOATTACH : SOUND_HIT);

	// check for death
	if(m_Health <= 0)
	{
		Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if(!InBattlefieldVehicle() && !WasVehiclePassenger &&
		VehicleTypeBeforeDamage != BATTLEFIELD_VEHICLE_UBOAT &&
		VehicleTypeBeforeDamage != BATTLEFIELD_VEHICLE_SHIP)
	{
		if(Dmg > 2)
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
		else
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

		m_EmoteType = EMOTE_PAIN;
		m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;
	}

	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	if(m_Invisible && SnappingClient != m_pPlayer->GetCID())
		return;
	if(NetworkClipped(SnappingClient))
		return;

	int EmoteType = m_EmoteType;
	if(m_EmoteStop < Server()->Tick())
	{
		EmoteType = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}

	int Health = 0, Armor = 0, AmmoCount = 0;
	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		Health = m_Health;
		Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(EmoteType == EMOTE_NORMAL)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			EmoteType = EMOTE_BLINK;
	}

	if(SnappingClient < 0 || !Server()->IsSixup(SnappingClient))
	{
		CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, m_pPlayer->GetCID(), sizeof(CNetObj_Character)));
		if(!pCharacter)
			return;

		if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
		{
			pCharacter->m_Tick = 0;
			m_Core.Write(pCharacter);
		}
		else
		{
			pCharacter->m_Tick = m_ReckoningTick;
			m_SendCore.Write(pCharacter);
		}

		pCharacter->m_Emote = EmoteType;
		pCharacter->m_AmmoCount = AmmoCount;
		pCharacter->m_Health = Health;
		pCharacter->m_Armor = Armor;
		pCharacter->m_Weapon = m_ActiveWeapon;
		pCharacter->m_AttackTick = m_AttackTick;
		pCharacter->m_Direction = m_Input.m_Direction;
		pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
	}
	else
	{
		protocol7::CNetObj_Character *pCharacter = static_cast<protocol7::CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, m_pPlayer->GetCID(), sizeof(protocol7::CNetObj_Character)));
		if(!pCharacter)
			return;

		if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
		{
			pCharacter->m_Tick = 0;
			m_Core.Write((CNetObj_CharacterCore *)pCharacter);
		}
		else
		{
			pCharacter->m_Tick = m_ReckoningTick;
			m_SendCore.Write((CNetObj_CharacterCore *)pCharacter);
		}

		// 0.7 rejects Character if Armor/Health > 10 (vehicle uses Armor for HP)
		pCharacter->m_Emote = EmoteType;
		pCharacter->m_AmmoCount = AmmoCount;
		pCharacter->m_Health = clamp(Health, 0, 10);
		pCharacter->m_Armor = clamp(Armor, 0, 10);
		pCharacter->m_Weapon = m_ActiveWeapon;
		pCharacter->m_AttackTick = m_AttackTick;
		pCharacter->m_Direction = m_Input.m_Direction;
		pCharacter->m_TriggeredEvents = TriggeredEvents_SixToSeven(m_Core.m_TriggeredEvents);
		// keep HookedPlayer=-1; 0 would mean hooked to CID 0 and break prediction
	}
}
