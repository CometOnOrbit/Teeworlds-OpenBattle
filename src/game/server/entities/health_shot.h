#ifndef GAME_SERVER_ENTITIES_HEALTH_SHOT_H
#define GAME_SERVER_ENTITIES_HEALTH_SHOT_H

#include <game/server/entity.h>

class CHealthShot : public CEntity
{
public:
	CHealthShot(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner, int Type);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void Hit();

	vec2 m_Direction;
	vec2 m_Vel;
	int m_Owner;
	int m_LifeSpan;
	int m_Type;
	float m_Friction;
	vec2 m_CurrentPos;
};

#endif
