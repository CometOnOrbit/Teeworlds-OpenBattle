#ifndef GAME_SERVER_ENTITIES_CK_DOOR_H
#define GAME_SERVER_ENTITIES_CK_DOOR_H

#include <game/server/entity.h>

// A DDNet-numbered laser door whose state is owned by CGameControllerCK.
class CCKDoor : public CEntity
{
	vec2 m_To;
	int m_Number;
	void SnapLaser(int ID, vec2 From, vec2 To);

public:
	CCKDoor(CGameWorld *pGameWorld, vec2 From, vec2 To, int Number);
	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	bool Intersects(vec2 From, vec2 To, vec2 *pHit, float Radius) const;
	int Number() const { return m_Number; }
};

#endif
