#ifndef GAME_SERVER_ENTITIES_GUIDED_PROJECTILE_H
#define GAME_SERVER_ENTITIES_GUIDED_PROJECTILE_H

#include <game/server/entity.h>

class CCharacter;

class CGuidedProjectile : public CEntity
{
public:
	CGuidedProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir,
		int Span, int Damage, bool Explosive, int CustomType, float Force,
		int SoundImpact, int Weapon);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void Impact(CCharacter *pTarget, bool Explode);

	vec2 m_Direction;
	vec2 m_Vel;
	int m_LifeSpan;
	int m_Owner;
	int m_Damage;
	bool m_Explosive;
	int m_CustomType;
	float m_Force;
	int m_SoundImpact;
	int m_Weapon;
	int m_Type;
	bool m_Ballistic;
	bool m_HasTarget;
	bool m_SavePosition;
	vec2 m_SavedPos;
};

#endif
