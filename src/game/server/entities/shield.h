#ifndef GAME_SERVER_ENTITIES_SHIELD_H
#define GAME_SERVER_ENTITIES_SHIELD_H

#include <game/server/entity.h>

class CShield : public CEntity
{
public:
	CShield(CGameWorld *pGameWorld, vec2 Pos, int Owner, int Damage, bool Explosive, int OrbitSlot);
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	int m_Owner;
	int m_Damage;
	bool m_Explosive;
	int m_OrbitSlot;
	int m_LifeTimer;
	vec2 m_Vel;
};

#endif
