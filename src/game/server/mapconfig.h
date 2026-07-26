#ifndef GAME_SERVER_MAPCONFIG_H
#define GAME_SERVER_MAPCONFIG_H

#include <engine/shared/jsonparser.h>

class CGameContext;

// Shared map metadata gate. The parser remains owned by the caller so a mode
// may inspect its own section after the common version/mode checks succeed.
class CMapConfig
{
public:
	static bool LoadForMode(CGameContext *pGameServer, const char *pMode, CJsonParser &Parser, json_value **ppRoot, char *pError, int ErrorSize);
};

#endif
