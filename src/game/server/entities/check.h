#ifndef GAME_SERVER_ENTITIES_CHECK_H
#define GAME_SERVER_ENTITIES_CHECK_H

#include <game/server/entity.h>

class CCheck : public CEntity
{
public:
	CCheck(CGameWorld *pGameWorld, vec2 From, vec2 To, int Type, int Team);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void DoBounce();
	void HitCharacter();
	void SnapLaser(int ID, vec2 From, vec2 To);

	int m_Type;
	vec2 m_To;
	int m_Team;
};

#endif
