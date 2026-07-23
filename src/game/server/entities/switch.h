#ifndef GAME_SERVER_ENTITIES_SWITCH_H
#define GAME_SERVER_ENTITIES_SWITCH_H

#include <game/server/entity.h>

class CSwitch : public CEntity
{
public:
	CSwitch(CGameWorld *pGameWorld, vec2 Pos, int Number);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	bool HitCharacter();
	void SnapLaser(int ID, vec2 From, vec2 To);
	void Toggle();

	int m_Number;
	int m_Cooldown;
	bool m_Hacking;
	vec2 m_HackerPos;
	int m_HackProgress;
};

#endif
