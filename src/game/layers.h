/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_LAYERS_H
#define GAME_LAYERS_H

#include <engine/map.h>
#include <game/mapitems.h>

class CLayers
{
	int m_GroupsNum;
	int m_GroupsStart;
	int m_LayersNum;
	int m_LayersStart;
	int m_GameLayerIndex;
	int m_SwitchLayerIndex;
	int m_TeleLayerIndex;
	CMapItemGroup *m_pGameGroup;
	CMapItemLayerTilemap *m_pGameLayer;
	CMapItemLayerTilemap *m_pSwitchLayer;
	CMapItemLayerTilemap *m_pTeleLayer;
	class IMap *m_pMap;

public:
	CLayers();
	void Init(class IKernel *pKernel);
	int NumGroups() const { return m_GroupsNum; };
	class IMap *Map() const { return m_pMap; };
	CMapItemGroup *GameGroup() const { return m_pGameGroup; };
	CMapItemLayerTilemap *GameLayer() const { return m_pGameLayer; };
	// Optional DDNet Switch layer. CK uses its TILE_SWITCHOPEN Number values
	// as its A-P objective areas.
	CMapItemLayerTilemap *SwitchLayer() const { return m_pSwitchLayer; };
	CMapItemLayerTilemap *TeleLayer() const { return m_pTeleLayer; };
	// DDNet stores CSwitchTile records in the Switch layer's m_Switch data
	// field, not in the normal CTile data slot of the visible layer.
	int SwitchData() const;
	// Tele layer's DDNet extension, holding CTeleTile records.
	int TeleData() const;
	CMapItemGroup *GetGroup(int Index) const;
	CMapItemLayer *GetLayer(int Index) const;
};

#endif
