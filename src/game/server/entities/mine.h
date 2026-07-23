#ifndef GAME_SERVER_ENTITIES_MINE_H
#define GAME_SERVER_ENTITIES_MINE_H

#include <game/server/entity.h>

class CMine : public CEntity
{
public:
	CMine(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void Hit();
	void SnapLaser(int ID, vec2 From, vec2 To);

	vec2 m_Vel;
	int m_Owner;
	bool m_Defusing;
	int m_DefuseProgress;
	vec2 m_DefuserPos;
	int m_BlinkTimer;
	vec2 m_CurrentPos;
};

#endif
