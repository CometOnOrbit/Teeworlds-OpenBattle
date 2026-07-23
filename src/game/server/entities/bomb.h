#ifndef GAME_SERVER_ENTITIES_BOMB_H
#define GAME_SERVER_ENTITIES_BOMB_H

#include <game/server/entity.h>

class CBomb : public CEntity
{
public:
	enum
	{
		TYPE_HELI_BOMB = 1,
		TYPE_UBOAT_TORPEDO = 10,
		TYPE_GRENADE = 11,
		TYPE_SHIP_SHELL = 12,
	};

	CBomb(CGameWorld *pGameWorld, vec2 Pos, int Owner, vec2 Direction, int Type, int Damage);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void TickHeliBomb();
	void TickTorpedo();
	void ExplodeHeliBomb();
	void ExplodeTorpedo(bool DirectHit);
	bool TouchesSolid(float Radius);

	vec2 m_Direction;
	vec2 m_Vel;
	vec2 m_aOrbitPos[3];
	int m_Owner;
	int m_Type;
	int m_Damage;
	int m_ReleaseTimer;
	int m_LifeTimer;
	int m_MoveTicks;
};

#endif
