/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "eventhandler.h"
#include "gamecontext.h"
#include "player.h"

//////////////////////////////////////////////////
// Event handler
//////////////////////////////////////////////////
CEventHandler::CEventHandler()
{
	m_pGameServer = 0;
	Clear();
}

void CEventHandler::SetGameServer(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
}

void *CEventHandler::Create(int Type, int Size, int Mask)
{
	if(m_NumEvents == MAX_EVENTS)
		return 0;
	if(m_CurrentOffset+Size >= MAX_DATASIZE)
		return 0;

	void *p = &m_aData[m_CurrentOffset];
	m_aOffsets[m_NumEvents] = m_CurrentOffset;
	m_aTypes[m_NumEvents] = Type;
	m_aSizes[m_NumEvents] = Size;
	m_aClientMasks[m_NumEvents] = Mask;
	m_CurrentOffset += Size;
	m_NumEvents++;
	return p;
}

void CEventHandler::Clear()
{
	m_NumEvents = 0;
	m_CurrentOffset = 0;
}

bool CEventHandler::OverrideEvent(int SnappingClient, int *pType, int *pSize, const char **ppData)
{
	static char s_aEventStore[128];
	bool Sixup = SnappingClient >= 0 && GameServer()->Server()->IsSixup(SnappingClient);

	if(*pType == NETEVENTTYPE_DAMAGEIND && Sixup)
	{
		const CNetEvent_DamageInd *pEvent = (const CNetEvent_DamageInd *)(*ppData);
		protocol7::CNetEvent_Damage *pEvent7 = (protocol7::CNetEvent_Damage *)s_aEventStore;
		*pType = -protocol7::NETEVENTTYPE_DAMAGE;
		*pSize = sizeof(*pEvent7);
		pEvent7->m_X = pEvent->m_X;
		pEvent7->m_Y = pEvent->m_Y;
		pEvent7->m_ClientID = -1;
		pEvent7->m_Angle = pEvent->m_Angle;
		pEvent7->m_HealthAmount = 1;
		pEvent7->m_ArmorAmount = 0;
		pEvent7->m_Self = 0;
		*ppData = s_aEventStore;
		return false; // use converted
	}
	return false;
}

void CEventHandler::Snap(int SnappingClient)
{
	for(int i = 0; i < m_NumEvents; i++)
	{
		if(SnappingClient == -1 || CmaskIsSet(m_aClientMasks[i], SnappingClient))
		{
			CNetEvent_Common *ev = (CNetEvent_Common *)&m_aData[m_aOffsets[i]];
			int Type = m_aTypes[i];
			int Size = m_aSizes[i];
			const char *pData = &m_aData[m_aOffsets[i]];
			if(OverrideEvent(SnappingClient, &Type, &Size, &pData))
				continue;

			if(SnappingClient == -1 || distance(GameServer()->m_apPlayers[SnappingClient]->m_ViewPos, vec2(ev->m_X, ev->m_Y)) < 1500.0f)
			{
				void *d = GameServer()->Server()->SnapNewItem(Type, i, Size);
				if(d)
					mem_copy(d, pData, Size);
			}
		}
	}
}
