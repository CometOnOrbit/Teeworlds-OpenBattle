#ifndef GAME_SERVER_ENTITIES_CK_POINT_FLAG_H
#define GAME_SERVER_ENTITIES_CK_POINT_FLAG_H

#include <game/server/entity.h>

// Cosmetic CK checkpoint flag. It only snapshots a Flag object and never
// participates in the CTF pickup/carry rules.
class CCKPointFlag : public CEntity
{
	int m_Number;

public:
	CCKPointFlag(CGameWorld *pGameWorld, vec2 Pos, int Number);
	virtual void Reset();
	virtual void Snap(int SnappingClient);
};

#endif
