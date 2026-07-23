/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_CHARACTER_H
#define GAME_SERVER_ENTITIES_CHARACTER_H

#include <game/server/entity.h>
#include <game/generated/server_data.h>
#include <game/generated/protocol.h>

#include <game/gamecore.h>

enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

class CCharacter : public CEntity
{
	MACRO_ALLOC_POOL_ID()
	friend class CGameWorld;

public:
	enum
	{
		BATTLEFIELD_VEHICLE_NONE = 0,
		BATTLEFIELD_VEHICLE_HELI = 1,
		BATTLEFIELD_VEHICLE_JET = 2,
		BATTLEFIELD_VEHICLE_PANZER = 3,
		BATTLEFIELD_VEHICLE_CAR = 4,
		BATTLEFIELD_VEHICLE_UBOAT = 9,
		BATTLEFIELD_VEHICLE_SHIP = 10,
		BATTLEFIELD_VEHICLE_MINI = 11,
	};

	//character's size
	static const int ms_PhysSize = 28;

	CCharacter(CGameWorld *pWorld);

	virtual void Reset();
	virtual void Destroy();
	virtual void Tick();
	virtual void TickDefered();
	virtual void Snap(int SnappingClient);

	bool IsGrounded();

	void SetWeapon(int W);
	void HandleWeaponSwitch();
	void DoWeaponSwitch();

	void HandleWeapons();
	void HandleNinja();
	void HandleTicks();
	void HandleTiles();
	void HandleOther();

	void OnPredictedInput(CNetObj_PlayerInput *pNewInput);
	void OnDirectInput(CNetObj_PlayerInput *pNewInput);
	void ResetInput();
	void FireWeapon();

	void Die(int Killer, int Weapon);
	bool TakeDamage(vec2 Force, int Dmg, int From, int Weapon);

	bool Spawn(class CPlayer *pPlayer, vec2 Pos);
	bool Remove();

	bool IncreaseHealth(int Amount);
	bool IncreaseArmor(int Amount);

	bool GiveWeapon(int Weapon, int Ammo);
	bool RefillBattlefieldWaterAmmo();
	bool RefillBattlefieldAmmo(bool StationarySupply = false);
	void ApplyBattlefieldLoadout();
	bool IsEngineerAction() const { return m_EngineerAction; }
	bool BattlefieldGodModeActive() const { return m_GodModeTimer > 0; }
	bool WantsC4Detonation() const;
	bool AmmoSupplyCoolingDown() const { return m_SelfAmmoCooldown > 0; }
	void StartAmmoSupplyCooldown();
	void ReleaseMineSlot();
	void ReleaseC4Slot();
	void ReleaseHealthKitSlot();
	void ReleaseAmmoPackSlot();
	void Act();
	void GiveNinja();
	void Check(int Checkpoint);
	void SetBattlefieldBroadcastTimer(int Ticks) { m_BroadcastClearTimer = Ticks; }
	void Tele(vec2 Pos);
	void CollideWithDoor(vec2 From, vec2 To);
	bool CanEnterBattlefieldVehicle() const;
	bool EnterBattlefieldVehicle(class CBattle *pVehicle, int Type, int Health);
	void RequestBattlefieldVehicleExit();
	void DetachBattlefieldVehicle(class CBattle *pVehicle);
	bool InBattlefieldVehicle() const { return m_BattlefieldVehicleType != BATTLEFIELD_VEHICLE_NONE; }
	bool IsBattlefieldVehiclePassenger() const { return m_BattlefieldVehiclePassenger; }
	int GetBattlefieldVehicleHealth() const { return m_BattlefieldVehicleHealth; }
	int GetBattlefieldVehicleType() const { return m_BattlefieldVehicleType; }
	bool RepairBattlefieldVehicle(int Amount);
	bool BattlefieldUboatShotArmed() const { return m_BattlefieldVehicleFireToggle; }
	void ResetBattlefieldUboatShot();
	void ResetBattlefieldHeliBomb() { m_BattlefieldHeliBombActive = false; }
	vec2 GetBattlefieldVehicleVelocity() const { return m_Core.m_Vel; }
	vec2 GetVelocity() const { return m_Core.m_Vel; }
	vec2 GetBattlefieldVehicleAim() const;
	int GetBattlefieldInputDirection() const { return m_Input.m_Direction; }
	bool BattlefieldJetSafelyStopped() const
	{
		return m_BattlefieldVehicleType == BATTLEFIELD_VEHICLE_JET &&
			m_BattlefieldVehicleLanding && m_BattlefieldVehicleSpeed <= 4;
	}
	bool InAntiTankCannon() const { return m_InAntiTankCannon; }
	bool InAntiAircraftCannon() const { return m_InAntiAircraftCannon; }
	int GetAntiTankMode() const { return m_AntiTankMode; }
	int GetAntiTankFireCooldown() const { return m_AntiTankFireCooldown; }
	vec2 GetAntiTankCursor() const { return m_AntiTankStationPos+vec2(m_Input.m_TargetX, m_Input.m_TargetY); }
	void SetAntiTankTarget(int TargetCID) { m_AntiTankTargetCID = TargetCID; }
	void ReleaseRemoteGrenadeControl(class CPak *pPak);
	void OnPakDestroyed(class CPak *pPak, int Mode);
	bool WantsSmokeLauncherFire() const { return m_SmokeLauncherFireRequested; }
	bool HasSmokeLauncher() const { return m_HasSmokeLauncher; }
	bool ClaimSmokeLauncher();
	void ReleaseSmokeLauncher()
	{
		m_HasSmokeLauncher = false;
		m_SmokeLauncherFireRequested = false;
	}
	vec2 GetSmokeLauncherAim() const;
	bool GetEngineerGrenadeControlState() const { return m_EngineerGrenadeMode; }
	void ResetEngineerGrenadeControlState() { m_EngineerGrenadeMode = false; }
	vec2 GetEngineerGrenadeAimPoint() const
	{
		return m_Pos+vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY);
	}
	void RegisterBattlefieldKill(bool CountKill);
	void HandGrenade();
	void HandleBattlefieldExitCommand();
	void ApplyBattlefieldSmokeEmote() { m_EmoteType = EMOTE_PAIN; }

	void SetEmote(int Emote, int Tick);

	bool IsAlive() const { return m_Alive; }
	int GetHealth() const { return m_Health; }
	class CPlayer *GetPlayer() { return m_pPlayer; }
	class CPlayer *GetPlayer() const { return m_pPlayer; }

private:
	void HandleBattlefieldTiles();
	void HandleBattlefieldWater();
	void ResetBattlefieldVehicleFromWater();
	void BlockBattlefieldTile(int Flags, const char *pMessage);
	void SelectBattlefieldClass(int Class);
	void SelectWaterWeapon(int Weapon);
	void HandleBattlefieldVehicle();
	void HandleBattlefieldPassenger();
	void PrepareBattlefieldVehicleWeaponState();
	void TickHeliGunner();
	void TickPanzerGunner();
	void Car(vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary);
	void JetFly(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary, bool &UnsafeJetLanding);
	void HeliFly(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary);
	void Panzer(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary);
	void Ship(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary);
	void Uboat(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed, bool &FirePrimary, bool &FireSecondary);
	void Mini(vec2 Aim, vec2 &Vel, vec2 &Pos, float &MaxSpeed);
	void RetractBattlefieldVehicleHook(bool ClearInputHook = true);
	void LeaveBattlefieldVehicle(bool Destroyed, bool ImmediateReset = false);
	bool EnterBattlefieldPassenger(CCharacter *pDriver);
	void FireBattlefieldVehicleWeapon(vec2 Direction, bool Alternate);
	void SendBattlefieldExtraProjectile(class CProjectile *pProjectile);
	void FireAirstrike(vec2 Direction);
	void HandleAntiTankCannon(int Tile, int TileIndex);
	void LeaveAntiTankCannon(bool PreserveStationLatch = false);
	void HandleAntiAircraftCannon(int Tile, int TileIndex);
	void LeaveAntiAircraftCannon(bool PreserveStationLatch = false);
	void EjectFromBattlefieldStation();
	void HandleBattlefieldSniperAbilities();
	void HandleBattlefieldEngineerAbilities();
	bool HandleBattlefieldMedicAbilities();
	void CreateVehicleDestructionEffects(int Type, vec2 Pos);
	void SendBattlefieldHitSound(int From, int Sound);

	// player controlling this character
	class CPlayer *m_pPlayer;

	bool m_Alive;

	// weapon info
	CEntity *m_apHitObjects[10];
	int m_NumObjectsHit;

	struct WeaponStat
	{
		int m_AmmoRegenStart;
		int m_Ammo;
		int m_Ammocost;
		bool m_Got;

	} m_aWeapons[NUM_WEAPONS];

	int m_ActiveWeapon;
	int m_LastWeapon;
	int m_QueuedWeapon;

	int m_ReloadTimer;
	int m_AttackTick;

	int m_DamageTaken;

	int m_EmoteType;
	int m_EmoteStop;

	// last tick that the player took any action ie some input
	int m_LastAction;

	// these are non-heldback inputs
	CNetObj_PlayerInput m_LatestPrevInput;
	CNetObj_PlayerInput m_LatestInput;

	// input
	CNetObj_PlayerInput m_PrevInput;
	CNetObj_PlayerInput m_Input;
	int m_NumInputs;
	int m_Jumped;

	int m_DamageTakenTick;

	int m_Health;
	int m_Armor;

	// Battlefield mod state.
	int m_BattlefieldTileCooldown;
	vec2 m_BattlefieldTileLastPos;
	int m_BroadcastClearTimer;
	bool m_EngineerAction;
	int m_EngineerToolCooldown;
	int m_NumMines;
	int m_NumC4;
	int m_MinesSinceSupply;
	int m_C4SinceSupply;
	int m_NumHealthKits;
	int m_NumAmmoPacks;
	int m_DeployCooldown;
	int m_SelfAmmoCooldown;
	int m_NinjaSwitchCooldown;
	bool m_NinjaMovementBlocked;
	int m_ClassGunMagazine;
	int m_SoldierGunMagazine;
	int m_HarpoonAmmo;
	int m_ArrowAmmo;
	int m_ThrowingStarAmmo;
	bool m_EngineerGrenadeMode;
	bool m_InBattlefieldWater;
	bool m_BattlefieldOxygenActive;
	int m_BattlefieldOxygenCooldown;
	int m_WaterWeaponCooldown;
	int m_HarpoonCharge;
	bool m_HarpoonCharging;
	class CBattle *m_pBattlefieldVehicle;
	CCharacter *m_pBattlefieldSeatDriver;
	CCharacter *m_pBattlefieldPassenger;
	bool m_BattlefieldVehiclePassenger;
	int m_BattlefieldVehicleType;
	int m_BattlefieldVehicleHealth;
	int m_BattlefieldVehicleWeaponCooldown;
	int m_BattlefieldVehicleAltCooldown;
	bool m_BattlefieldVehicleFireToggle;
	bool m_BattlefieldHeliBombActive;
	int m_BattlefieldVehicleEntryCooldown;
	int m_BattlefieldRepairCooldown;
	int m_EngineerObstructedCooldown;
	int m_OccupiedVehicleAmmoRegenCooldown;
	int m_BattlefieldVehicleSpeed;
	int m_BattlefieldVehicleAmmo;
	int m_BattlefieldVehicleBurstShots;
	int m_BattlefieldVehicleAmmoRegen;
	int m_BattlefieldVehicleOverheat;
	int m_BattlefieldVehicleMoveSound;
	int m_BattlefieldVehicleGrace;
	int m_HeliCollisionGrace;
	int m_HeliPassengerEntryDelay;
	int m_HeliBombCooldown;
	int m_JetFeedbackCooldown;
	int m_BattlefieldVehicleLandingCounter;
	bool m_BattlefieldVehicleAirborne;
	bool m_BattlefieldVehicleSafeLanding;
	bool m_BattlefieldVehicleLanding;
	bool m_BattlefieldVehicleExitRequested;
	bool m_BattlefieldVehicleWaterResetPending;
	bool m_BattlefieldVehicleScoreboardHeld;
	bool m_BattlefieldUboatOutsideWater;
	bool m_BattlefieldUboatCollisionLatched;
	bool m_InAntiTankCannon;
	bool m_AntiTankStationLatched;
	bool m_InAntiAircraftCannon;
	// Stays set for one HandleTiles pass after clearing Anti-Aircraft state so
	// that pass ejects the player instead of remounting the same cannon.
	bool m_AntiAircraftStationLatched;
	vec2 m_AntiAircraftStationPos;
	int m_AntiAircraftBurstCount;
	vec2 m_AntiTankStationPos;
	int m_AntiTankMode;
	int m_AntiTankTargetCID;
	int m_AntiTankFireCooldown;
	class CPak *m_pAntiTankTargeter;
	class CPak *m_pAntiTankRemoteGrenade;
	int m_KillStreak;
	int m_GodModeTimer;
	bool m_HandGrenadeAvailable;
	int m_AirstrikeBurstTimer;
	int m_AirstrikeCooldown;
	int m_AirstrikeShotCooldown;
	bool m_Invisible;
	int m_InvisibilityPower;
	int m_MedicRegenCooldown;
	int m_MedicDamageCooldown;
	int m_MedicHealCooldown;
	int m_MedicHealSecondCooldown;
	bool m_HasSmokeLauncher;
	bool m_SmokeLauncherFireRequested;

	// ninja
	struct
	{
		vec2 m_ActivationDir;
		int m_ActivationTick;
		int m_CurrentMoveTime;
		int m_OldVelAmount;
	} m_Ninja;

	// the player core for the physics
	CCharacterCore m_Core;

	// info for dead reckoning
	int m_ReckoningTick; // tick that we are performing dead reckoning From
	CCharacterCore m_SendCore; // core that we should send
	CCharacterCore m_ReckoningCore; // the dead reckoning core

};

#endif
