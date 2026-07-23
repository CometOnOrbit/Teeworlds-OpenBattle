#ifndef GAME_SERVER_ENTITIES_SMOKE_H
#define GAME_SERVER_ENTITIES_SMOKE_H

#include <game/server/entity.h>

class CSmoke : public CEntity
{
public:
	enum
	{
		TYPE_CLOUD = 2,
		TYPE_LAUNCHER = 3,
	};

	CSmoke(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner, int Type);
	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void TickCloud();
	void TickLauncher();
	void SpawnCloudBurst();
	void SnapProjectile(int ID, vec2 Pos);
	void SnapLaser(int ID, vec2 From, vec2 To);

	vec2 m_BasePos;
	vec2 m_BaseVel;
	vec2 m_ProjectilePos1;
	vec2 m_ProjectilePos2;
	vec2 m_Direction;
	vec2 m_Vel;
	int m_Owner;
	int m_Type;
	int m_LifeTimer;
	int m_UserCID;
	int m_FiringTimer;
	int m_FiringDelay;
	int m_BurstTimer;
	int m_OscillationTimer;
	bool m_OscillationPositive;
};

#endif
