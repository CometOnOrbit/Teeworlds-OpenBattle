/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "layers.h"

CLayers::CLayers()
{
	m_GroupsNum = 0;
	m_GroupsStart = 0;
	m_LayersNum = 0;
	m_LayersStart = 0;
	m_GameLayerIndex = -1;
	m_SwitchLayerIndex = -1;
	m_TeleLayerIndex = -1;
	m_pGameGroup = 0;
	m_pGameLayer = 0;
	m_pSwitchLayer = 0;
	m_pTeleLayer = 0;
	m_pMap = 0;
}

void CLayers::Init(class IKernel *pKernel)
{
	m_pMap = pKernel->RequestInterface<IMap>();
	m_pMap->GetType(MAPITEMTYPE_GROUP, &m_GroupsStart, &m_GroupsNum);
	m_pMap->GetType(MAPITEMTYPE_LAYER, &m_LayersStart, &m_LayersNum);

	for(int g = 0; g < NumGroups(); g++)
	{
		CMapItemGroup *pGroup = GetGroup(g);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = GetLayer(pGroup->m_StartLayer+l);

			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTilemap = reinterpret_cast<CMapItemLayerTilemap *>(pLayer);
				char aName[13];
				mem_copy(aName, pTilemap->m_aName, 12);
				aName[12] = 0;
				int LayerIndex = pGroup->m_StartLayer+l;
				if((pTilemap->m_Flags&TILESLAYERFLAG_SWITCH) || str_comp_nocase(aName, "Switch") == 0)
				{
					m_pSwitchLayer = pTilemap;
					m_SwitchLayerIndex = LayerIndex;
				}
				if(pTilemap->m_Flags&TILESLAYERFLAG_TELE)
				{
					m_pTeleLayer = pTilemap;
					m_TeleLayerIndex = LayerIndex;
				}
				if(pTilemap->m_Flags&1)
				{
					m_pGameLayer = pTilemap;
					m_GameLayerIndex = LayerIndex;
					m_pGameGroup = pGroup;

					// make sure the game group has standard settings
					m_pGameGroup->m_OffsetX = 0;
					m_pGameGroup->m_OffsetY = 0;
					m_pGameGroup->m_ParallaxX = 100;
					m_pGameGroup->m_ParallaxY = 100;

					if(m_pGameGroup->m_Version >= 2)
					{
						m_pGameGroup->m_UseClipping = 0;
						m_pGameGroup->m_ClipX = 0;
						m_pGameGroup->m_ClipY = 0;
						m_pGameGroup->m_ClipW = 0;
						m_pGameGroup->m_ClipH = 0;
					}

				}
			}
		}
	}
}

CMapItemGroup *CLayers::GetGroup(int Index) const
{
	return static_cast<CMapItemGroup *>(m_pMap->GetItem(m_GroupsStart+Index, 0, 0));
}

CMapItemLayer *CLayers::GetLayer(int Index) const
{
	return static_cast<CMapItemLayer *>(m_pMap->GetItem(m_LayersStart+Index, 0, 0));
}

int CLayers::SwitchData() const
{
	// m_SwitchLayerIndex is relative to MAPITEMTYPE_LAYER. GetItemSize expects
	// the absolute map-item index, just like GetLayer above.
	if(!m_pSwitchLayer || m_SwitchLayerIndex < 0 || m_pMap->GetItemSize(m_LayersStart+m_SwitchLayerIndex) < (int)sizeof(CMapItemLayerTilemapDDNet))
		return m_pSwitchLayer && m_pSwitchLayer->m_Data >= 0 && m_pSwitchLayer->m_Data < m_pMap->NumData() ? m_pSwitchLayer->m_Data : -1;
	const CMapItemLayerTilemapDDNet *pSwitchLayer = reinterpret_cast<const CMapItemLayerTilemapDDNet *>(m_pSwitchLayer);
	if(pSwitchLayer->m_Switch >= 0 && pSwitchLayer->m_Switch < m_pMap->NumData())
		return pSwitchLayer->m_Switch;
	// Some version-3 DDNet maps mark the Switch layer with its layer flag and
	// store CSwitchTile directly in m_Data instead of a separate m_Switch index.
	return m_pSwitchLayer->m_Data >= 0 && m_pSwitchLayer->m_Data < m_pMap->NumData() ? m_pSwitchLayer->m_Data : -1;
}

int CLayers::TeleData() const
{
	if(!m_pTeleLayer || m_TeleLayerIndex < 0 || m_pMap->GetItemSize(m_LayersStart+m_TeleLayerIndex) < (int)sizeof(CMapItemLayerTilemapDDNet))
		return m_pTeleLayer && m_pTeleLayer->m_Data >= 0 && m_pTeleLayer->m_Data < m_pMap->NumData() ? m_pTeleLayer->m_Data : -1;
	const CMapItemLayerTilemapDDNet *pTeleLayer = reinterpret_cast<const CMapItemLayerTilemapDDNet *>(m_pTeleLayer);
	if(pTeleLayer->m_Tele >= 0 && pTeleLayer->m_Tele < m_pMap->NumData())
		return pTeleLayer->m_Tele;
	return m_pTeleLayer->m_Data >= 0 && m_pTeleLayer->m_Data < m_pMap->NumData() ? m_pTeleLayer->m_Data : -1;
}
