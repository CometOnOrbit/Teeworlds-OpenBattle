#include <engine/shared/config.h>
#include <engine/storage.h>

#include <base/system.h>

#include "gamecontext.h"
#include "mapconfig.h"

bool CMapConfig::LoadForMode(CGameContext *pGameServer, const char *pMode, CJsonParser &Parser, json_value **ppRoot, char *pError, int ErrorSize)
{
	const char *pMap = g_Config.m_SvMap;
	for(const char *p = pMap; *p; ++p)
		if(*p == '/' || *p == '\\') pMap = p+1;
	char aPath[256];
	str_format(aPath, sizeof(aPath), "maps/%s.battle.json", pMap);
	IStorage *pStorage = pGameServer->Storage();
	IOHANDLE File = pStorage ? pStorage->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_ALL) : 0;
	if(!File)
	{
		str_format(pError, ErrorSize, "missing map config %s", aPath);
		return false;
	}
	int Size = (int)io_length(File);
	char *pData = new char[Size+1];
	io_read(File, pData, Size); pData[Size] = 0; io_close(File);
	json_value *pRoot = Parser.ParseData(pData, Size, aPath);
	delete[] pData;
	if(!pRoot || pRoot->type != json_object)
	{
		str_format(pError, ErrorSize, "invalid map config JSON: %s", Parser.Error());
		return false;
	}
	const json_value &Version = (*pRoot)["version"];
	const json_value &Modes = (*pRoot)["modes"];
	if(Version.type != json_integer || Version.u.integer != 1 || Modes.type != json_array)
	{
		str_copy(pError, "map config needs version 1 and modes", ErrorSize);
		return false;
	}
	bool Supported = false;
	for(unsigned i = 0; i < Modes.u.array.length; ++i)
		if(Modes[i].type == json_string && str_comp_nocase(Modes[i].u.string.ptr, pMode) == 0)
			Supported = true;
	if(!Supported)
	{
		str_format(pError, ErrorSize, "map config does not allow mode %s", pMode);
		return false;
	}
	*ppRoot = pRoot;
	return true;
}
