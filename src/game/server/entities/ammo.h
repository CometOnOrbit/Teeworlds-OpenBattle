#ifndef GAME_SERVER_ENTITIES_AMMO_H
#define GAME_SERVER_ENTITIES_AMMO_H

#include <game/server/entity.h>

class CAmmo : public CEntity
{
public:
	CAmmo(CGameWorld *pGameWorld, vec2 Pos, int Owner);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void Hit();

	vec2 m_Vel;
	int m_Owner;
	int m_AnimTimer;
	vec2 m_CurrentPos;
	int m_MessageCooldown;
};

#endif
