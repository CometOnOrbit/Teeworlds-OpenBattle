#ifndef GAME_SERVER_ENTITIES_C4_H
#define GAME_SERVER_ENTITIES_C4_H

#include <game/server/entity.h>

class CC4 : public CEntity
{
public:
	CC4(CGameWorld *pGameWorld, vec2 Pos, int Owner, vec2 Vel);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void Explode();
	void SnapLaser(int ID, vec2 From, vec2 To);

	vec2 m_CurrentPos;
	vec2 m_InitialVel;
	vec2 m_CurrentVel;
	int m_Owner;
	bool m_CollisionLatched;
	bool m_Attached;
	bool m_Defusing;
	int m_DefuseProgress;
	vec2 m_DefuserPos;
};

#endif
