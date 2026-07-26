#ifndef GAME_SERVER_ENTITIES_CK_TELEPORT_MARKER_H
#define GAME_SERVER_ENTITIES_CK_TELEPORT_MARKER_H

#include <game/server/entity.h>

// Visual-only laser above an active CK Tele In. Side is 0 for attackers and
// 1 for defenders; the map's Tele Number selects both side and CP number.
class CCKTeleportMarker : public CEntity
{
	vec2 m_To;
	int m_Number;
	int m_Side;

public:
	CCKTeleportMarker(CGameWorld *pGameWorld, vec2 From, vec2 To, int Number, int Side);
	virtual void Reset();
	virtual void Snap(int SnappingClient);
};

#endif
