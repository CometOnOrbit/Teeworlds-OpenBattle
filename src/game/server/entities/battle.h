#ifndef GAME_SERVER_ENTITIES_BATTLE_H
#define GAME_SERVER_ENTITIES_BATTLE_H

#include <game/server/entity.h>

class CCharacter;

class CBattle : public CEntity
{
public:
	enum
	{
		TYPE_HELI = 1,
		TYPE_JET = 2,
		TYPE_PANZER = 3,
		TYPE_CAR = 4,
		TYPE_UBOAT = 9,
		TYPE_SHIP = 10,
		TYPE_MINI = 11,
	};

	CBattle(CGameWorld *pGameWorld, vec2 Pos, int Type, int KeyTeam);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

	void OnDriverLeft(CCharacter *pDriver, bool Destroyed, bool ImmediateReset);
	int GetType() const { return m_Type; }
	int GetHealth() const { return m_Health; }

	static int MaxHealth(int Type);
	static const char *TypeName(int Type);

private:
	void ResetToSpawn(bool ApplyEnterCooldown = false);
	void TryEnter();
	void TryRepair();
	void CrashAbandonedJet();
	bool TeamCanUse(const CCharacter *pCharacter) const;
	void SnapLaser(int ID, vec2 From, vec2 To, int StartTickOffset = 0);
	void SnapPickup(int ID, vec2 Pos, int Type);
	void SnapProjectile(int ID, vec2 Pos, int Type, int StartTickOffset = 1);

	vec2 m_SpawnPos;
	vec2 m_PhysicalPos;
	vec2 m_Vel;
	CCharacter *m_pDriver;
	int m_Type;
	int m_KeyTeam;
	int m_SnapClient;
	int m_Health;
	int m_RespawnTimer;
	int m_EnterCooldown;
	int m_RepairCooldown;
	int m_MessageCooldown;
	int m_LastDriverCID;
	bool m_JetSafelyStopped;
	bool m_Repairing;
	vec2 m_RepairerPos;
	float m_SnapshotTilt;
	int m_SnapshotTiltCooldown;
};

#endif
