#ifndef GAME_SERVER_ENTITIES_HEALTH_H
#define GAME_SERVER_ENTITIES_HEALTH_H

#include <game/server/entity.h>

class CHealth : public CEntity
{
public:
	CHealth(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void Hit();

	vec2 m_Vel;
	int m_Owner;
	int m_FirstCooldown;
	int m_SecondCooldown;
	int m_NumShots;
	vec2 m_CurrentPos;
};

#endif
