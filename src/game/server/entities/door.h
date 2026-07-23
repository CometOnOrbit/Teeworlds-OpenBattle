#ifndef GAME_SERVER_ENTITIES_DOOR_H
#define GAME_SERVER_ENTITIES_DOOR_H

#include <game/server/entity.h>

class CDoor : public CEntity
{
public:
	CDoor(CGameWorld *pGameWorld, vec2 From, vec2 To, int Number);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void DoBounce();
	bool HitCharacter();
	void SnapLaser(int ID, vec2 From, vec2 To);

	vec2 m_To;
	int m_Number;
};

#endif
