/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/hash_ctxt.h>
#include <base/system.h>
#include "config.h"
#include "network.h"

#define MACRO_LIST_LINK_FIRST(Object, First, Prev, Next) \
	{ if(First) First->Prev = Object; \
	Object->Prev = (struct CBan *)0; \
	Object->Next = First; \
	First = Object; }

#define MACRO_LIST_LINK_AFTER(Object, After, Prev, Next) \
	{ Object->Prev = After; \
	Object->Next = After->Next; \
	After->Next = Object; \
	if(Object->Next) \
		Object->Next->Prev = Object; \
	}

#define MACRO_LIST_UNLINK(Object, First, Prev, Next) \
	{ if(Object->Next) Object->Next->Prev = Object->Prev; \
	if(Object->Prev) Object->Prev->Next = Object->Next; \
	else First = Object->Next; \
	Object->Next = 0; Object->Prev = 0; }

#define MACRO_LIST_FIND(Start, Next, Expression) \
	{ while(Start && !(Expression)) Start = Start->Next; }

bool CNetServer::Open(NETADDR BindAddr, int MaxClients, int MaxClientsPerIP, int Flags)
{
	// zero out the whole structure
	mem_zero(this, sizeof(*this));

	// open socket
	m_Socket = net_udp_create(BindAddr);
	if(!m_Socket.type)
		return false;

	// clamp clients
	m_MaxClients = MaxClients;
	if(m_MaxClients > NET_MAX_CLIENTS)
		m_MaxClients = NET_MAX_CLIENTS;
	if(m_MaxClients < 1)
		m_MaxClients = 1;

	m_MaxClientsPerIP = MaxClientsPerIP;

	secure_random_fill(m_aSecurityTokenSeed, sizeof(m_aSecurityTokenSeed));

	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		m_aSlots[i].m_Connection.Init(m_Socket);

	// setup all pointers for bans
	for(int i = 1; i < NET_SERVER_MAXBANS-1; i++)
	{
		m_BanPool[i].m_pNext = &m_BanPool[i+1];
		m_BanPool[i].m_pPrev = &m_BanPool[i-1];
	}

	m_BanPool[0].m_pNext = &m_BanPool[1];
	m_BanPool[NET_SERVER_MAXBANS-1].m_pPrev = &m_BanPool[NET_SERVER_MAXBANS-2];
	m_BanPool_FirstFree = &m_BanPool[0];

	return true;
}

int CNetServer::SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	m_pfnNewClient = pfnNewClient;
	m_pfnDelClient = pfnDelClient;
	m_UserPtr = pUser;
	return 0;
}

int CNetServer::Close()
{
	// TODO: implement me
	return 0;
}

int CNetServer::Drop(int ClientID, const char *pReason)
{
	// TODO: insert lots of checks here
	/*NETADDR Addr = ClientAddr(ClientID);

	dbg_msg("net_server", "client dropped. cid=%d ip=%d.%d.%d.%d reason=\"%s\"",
		ClientID,
		Addr.ip[0], Addr.ip[1], Addr.ip[2], Addr.ip[3],
		pReason
		);*/
	if(m_pfnDelClient)
		m_pfnDelClient(ClientID, pReason, m_UserPtr);

	m_aSlots[ClientID].m_Connection.Disconnect(pReason);

	return 0;
}

int CNetServer::BanGet(int Index, CBanInfo *pInfo)
{
	CBan *pBan;
	for(pBan = m_BanPool_FirstUsed; pBan && Index; pBan = pBan->m_pNext, Index--)
		{}

	if(!pBan)
		return 0;
	*pInfo = pBan->m_Info;
	return 1;
}

int CNetServer::BanNum()
{
	int Count = 0;
	CBan *pBan;
	for(pBan = m_BanPool_FirstUsed; pBan; pBan = pBan->m_pNext)
		Count++;
	return Count;
}

void CNetServer::BanRemoveByObject(CBan *pBan)
{
	int IpHash = (pBan->m_Info.m_Addr.ip[0]+pBan->m_Info.m_Addr.ip[1]+pBan->m_Info.m_Addr.ip[2]+pBan->m_Info.m_Addr.ip[3]+
					pBan->m_Info.m_Addr.ip[4]+pBan->m_Info.m_Addr.ip[5]+pBan->m_Info.m_Addr.ip[6]+pBan->m_Info.m_Addr.ip[7]+
					pBan->m_Info.m_Addr.ip[8]+pBan->m_Info.m_Addr.ip[9]+pBan->m_Info.m_Addr.ip[10]+pBan->m_Info.m_Addr.ip[11]+
					pBan->m_Info.m_Addr.ip[12]+pBan->m_Info.m_Addr.ip[13]+pBan->m_Info.m_Addr.ip[14]+pBan->m_Info.m_Addr.ip[15])&0xff;
	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(&pBan->m_Info.m_Addr, aAddrStr, sizeof(aAddrStr));
	dbg_msg("netserver", "removing ban on %s", aAddrStr);
	MACRO_LIST_UNLINK(pBan, m_BanPool_FirstUsed, m_pPrev, m_pNext);
	MACRO_LIST_UNLINK(pBan, m_aBans[IpHash], m_pHashPrev, m_pHashNext);
	MACRO_LIST_LINK_FIRST(pBan, m_BanPool_FirstFree, m_pPrev, m_pNext);
}

int CNetServer::BanRemove(NETADDR Addr)
{
	int IpHash = (Addr.ip[0]+Addr.ip[1]+Addr.ip[2]+Addr.ip[3]+Addr.ip[4]+Addr.ip[5]+Addr.ip[6]+Addr.ip[7]+
					Addr.ip[8]+Addr.ip[9]+Addr.ip[10]+Addr.ip[11]+Addr.ip[12]+Addr.ip[13]+Addr.ip[14]+Addr.ip[15])&0xff;
	CBan *pBan = m_aBans[IpHash];

	MACRO_LIST_FIND(pBan, m_pHashNext, net_addr_comp(&pBan->m_Info.m_Addr, &Addr) == 0);

	if(pBan)
	{
		BanRemoveByObject(pBan);
		return 0;
	}

	return -1;
}

int CNetServer::BanAdd(NETADDR Addr, int Seconds, const char *pReason)
{
	int IpHash = (Addr.ip[0]+Addr.ip[1]+Addr.ip[2]+Addr.ip[3]+Addr.ip[4]+Addr.ip[5]+Addr.ip[6]+Addr.ip[7]+
					Addr.ip[8]+Addr.ip[9]+Addr.ip[10]+Addr.ip[11]+Addr.ip[12]+Addr.ip[13]+Addr.ip[14]+Addr.ip[15])&0xff;
	int Stamp = -1;
	CBan *pBan;

	// remove the port
	Addr.port = 0;

	if(Seconds)
		Stamp = time_timestamp() + Seconds;

	// search to see if it already exists
	pBan = m_aBans[IpHash];
	MACRO_LIST_FIND(pBan, m_pHashNext, net_addr_comp(&pBan->m_Info.m_Addr, &Addr) == 0);
	if(pBan)
	{
		// adjust the ban
		pBan->m_Info.m_Expires = Stamp;
		return 0;
	}

	if(!m_BanPool_FirstFree)
		return -1;

	// fetch and clear the new ban
	pBan = m_BanPool_FirstFree;
	MACRO_LIST_UNLINK(pBan, m_BanPool_FirstFree, m_pPrev, m_pNext);

	// setup the ban info
	pBan->m_Info.m_Expires = Stamp;
	pBan->m_Info.m_Addr = Addr;
	str_copy(pBan->m_Info.m_Reason, pReason, sizeof(pBan->m_Info.m_Reason));

	// add it to the ban hash
	MACRO_LIST_LINK_FIRST(pBan, m_aBans[IpHash], m_pHashPrev, m_pHashNext);

	// insert it into the used list
	{
		if(m_BanPool_FirstUsed)
		{
			CBan *pInsertAfter = m_BanPool_FirstUsed;
			MACRO_LIST_FIND(pInsertAfter, m_pNext, Stamp < pInsertAfter->m_Info.m_Expires);

			if(pInsertAfter)
				pInsertAfter = pInsertAfter->m_pPrev;
			else
			{
				// add to last
				pInsertAfter = m_BanPool_FirstUsed;
				while(pInsertAfter->m_pNext)
					pInsertAfter = pInsertAfter->m_pNext;
			}

			if(pInsertAfter)
			{
				MACRO_LIST_LINK_AFTER(pBan, pInsertAfter, m_pPrev, m_pNext);
			}
			else
			{
				MACRO_LIST_LINK_FIRST(pBan, m_BanPool_FirstUsed, m_pPrev, m_pNext);
			}
		}
		else
		{
			MACRO_LIST_LINK_FIRST(pBan, m_BanPool_FirstUsed, m_pPrev, m_pNext);
		}
	}

	// drop banned clients
	{
		char Buf[128];
		NETADDR BanAddr;

		if(Stamp > -1)
		{
			int Mins = (Seconds + 59) / 60;
			if(Mins <= 1)
				str_format(Buf, sizeof(Buf), "You have been banned for 1 minute (%s)", pReason);
			else
				str_format(Buf, sizeof(Buf), "You have been banned for %d minutes (%s)", Mins, pReason);
		}
		else
			str_format(Buf, sizeof(Buf), "You have been banned for life (%s)", pReason);

		for(int i = 0; i < MaxClients(); i++)
		{
			BanAddr = m_aSlots[i].m_Connection.PeerAddress();
			BanAddr.port = 0;

			if(net_addr_comp(&Addr, &BanAddr) == 0)
				Drop(i, Buf);
		}
	}
	return 0;
}

int CNetServer::Update()
{
	int Now = time_timestamp();
	for(int i = 0; i < MaxClients(); i++)
	{
		m_aSlots[i].m_Connection.Update();
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_ERROR)
			Drop(i, m_aSlots[i].m_Connection.ErrorString());
	}

	// remove expired bans
	while(m_BanPool_FirstUsed && m_BanPool_FirstUsed->m_Info.m_Expires > -1 && m_BanPool_FirstUsed->m_Info.m_Expires < Now)
	{
		CBan *pBan = m_BanPool_FirstUsed;
		BanRemoveByObject(pBan);
	}

	return 0;
}

SECURITY_TOKEN CNetServer::GetToken(const NETADDR &Addr)
{
	SHA256_CTX Sha256;
	sha256_init(&Sha256);
	sha256_update(&Sha256, m_aSecurityTokenSeed, sizeof(m_aSecurityTokenSeed));
	sha256_update(&Sha256, &Addr, 20); // omit port, same as DDNet

	SECURITY_TOKEN SecurityToken = ToSecurityToken(sha256_finish(&Sha256).data);
	if(SecurityToken == NET_SECURITY_TOKEN_UNKNOWN || SecurityToken == NET_SECURITY_TOKEN_UNSUPPORTED)
		SecurityToken = 1;
	return SecurityToken;
}

SECURITY_TOKEN CNetServer::GetGlobalToken()
{
	static const NETADDR NullAddr = {0};
	return GetToken(NullAddr);
}

int CNetServer::GetClientSlot(const NETADDR &Addr)
{
	for(int i = 0; i < MaxClients(); i++)
	{
		if(m_aSlots[i].m_Connection.State() != NET_CONNSTATE_OFFLINE &&
			m_aSlots[i].m_Connection.State() != NET_CONNSTATE_ERROR)
		{
			NETADDR Peer = m_aSlots[i].m_Connection.PeerAddress();
			if(net_addr_comp(&Peer, &Addr) == 0)
				return i;
		}
	}
	return -1;
}

int CNetServer::TryAcceptClient(NETADDR &Addr, SECURITY_TOKEN SecurityToken, bool VanillaAuth, bool Sixup, SECURITY_TOKEN Token)
{
	(void)VanillaAuth;
	if(Sixup && !g_Config.m_SvSixup)
	{
		const char aMsg[] = "0.7 connections are not accepted at this time";
		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aMsg, sizeof(aMsg), SecurityToken, Sixup);
		return -1;
	}

	NETADDR ThisAddr = Addr;
	ThisAddr.port = 0;
	int FoundAddr = 0;
	for(int i = 0; i < MaxClients(); ++i)
	{
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
			continue;
		NETADDR Other = m_aSlots[i].m_Connection.PeerAddress();
		Other.port = 0;
		if(net_addr_comp(&ThisAddr, &Other) == 0)
			FoundAddr++;
	}
	if(FoundAddr >= m_MaxClientsPerIP)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Only %d players with the same IP are allowed", m_MaxClientsPerIP);
		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, SecurityToken, Sixup);
		return -1;
	}

	int Slot = -1;
	for(int i = 0; i < MaxClients(); i++)
	{
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
		{
			Slot = i;
			break;
		}
	}
	if(Slot == -1)
	{
		const char aFullMsg[] = "This server is full";
		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, aFullMsg, sizeof(aFullMsg), SecurityToken, Sixup);
		return -1;
	}

	m_aSlots[Slot].m_Connection.DirectInit(Addr, SecurityToken, Token, Sixup);
	if(m_pfnNewClient)
		m_pfnNewClient(Slot, m_UserPtr, Sixup);
	return Slot;
}

int CNetServer::OnSixupCtrlMsg(NETADDR &Addr, CNetChunk *pChunk, int ControlMsg, const CNetPacketConstruct &Packet, SECURITY_TOKEN &ResponseToken, SECURITY_TOKEN Token)
{
	if(m_RecvUnpacker.m_Data.m_DataSize < 5 || ClientExists(Addr))
		return 0;

	ResponseToken = ToSecurityToken(Packet.m_aChunkData + 1);

	if(ControlMsg == 5)
	{
		if(m_RecvUnpacker.m_Data.m_DataSize >= 512)
		{
			SendTokenSixup(Addr, ResponseToken);
			return 0;
		}
		pChunk->m_Flags = 0;
		pChunk->m_ClientID = -1;
		pChunk->m_Address = Addr;
		pChunk->m_DataSize = 0;
		return 1;
	}
	else if(ControlMsg == NET_CTRLMSG_CONNECT)
	{
		SECURITY_TOKEN MyToken = GetToken(Addr);
		unsigned char aToken[4];
		WriteSecurityToken(aToken, MyToken);
		CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CONNECTACCEPT, aToken, sizeof(aToken), ResponseToken, true);
		if(Token == MyToken)
			TryAcceptClient(Addr, ResponseToken, false, true, Token);
	}
	return 0;
}

void CNetServer::SendTokenSixup(NETADDR &Addr, SECURITY_TOKEN Token)
{
	SECURITY_TOKEN MyToken = GetToken(Addr);
	unsigned char aBuf[512];
	mem_zero(aBuf, sizeof(aBuf));
	WriteSecurityToken(aBuf, MyToken);
	int Size = (Token == NET_SECURITY_TOKEN_UNKNOWN) ? 512 : 4;
	CNetBase::SendControlMsg(m_Socket, &Addr, 0, 5, aBuf, Size, Token, true);
}

int CNetServer::SendConnlessSixup(CNetChunk *pChunk, SECURITY_TOKEN ResponseToken)
{
	if(pChunk->m_DataSize > NET_MAX_PACKETSIZE - 9)
		return -1;

	unsigned char aBuffer[NET_MAX_PACKETSIZE];
	aBuffer[0] = (NET_PACKETFLAG_CONNLESS << 2) | 1;
	SECURITY_TOKEN Token = GetToken(pChunk->m_Address);
	WriteSecurityToken(aBuffer + 1, ResponseToken);
	WriteSecurityToken(aBuffer + 5, Token);
	mem_copy(aBuffer + 9, pChunk->m_pData, pChunk->m_DataSize);
	net_udp_send(m_Socket, &pChunk->m_Address, aBuffer, pChunk->m_DataSize + 9);
	return 0;
}

/*
	TODO: chopp up this function into smaller working parts
*/
int CNetServer::Recv(CNetChunk *pChunk)
{
	unsigned Now = time_timestamp();

	while(1)
	{
		NETADDR Addr;

		if(m_RecvUnpacker.FetchChunk(pChunk))
			return 1;

		int Bytes = net_udp_recv(m_Socket, &Addr, m_RecvUnpacker.m_aBuffer, NET_MAX_PACKETSIZE);
		if(Bytes <= 0)
			break;

		SECURITY_TOKEN Token = NET_SECURITY_TOKEN_UNSUPPORTED;
		SECURITY_TOKEN ResponseToken = NET_SECURITY_TOKEN_UNKNOWN;
		bool Sixup = false;
		if(CNetBase::UnpackPacket(m_RecvUnpacker.m_aBuffer, Bytes, &m_RecvUnpacker.m_Data, Sixup, &Token, &ResponseToken) != 0)
			continue;

		CBan *pBan = 0;
		NETADDR BanAddr = Addr;
		int IpHash = (BanAddr.ip[0]+BanAddr.ip[1]+BanAddr.ip[2]+BanAddr.ip[3]+BanAddr.ip[4]+BanAddr.ip[5]+BanAddr.ip[6]+BanAddr.ip[7]+
						BanAddr.ip[8]+BanAddr.ip[9]+BanAddr.ip[10]+BanAddr.ip[11]+BanAddr.ip[12]+BanAddr.ip[13]+BanAddr.ip[14]+BanAddr.ip[15])&0xff;
		BanAddr.port = 0;
		for(pBan = m_aBans[IpHash]; pBan; pBan = pBan->m_pHashNext)
		{
			if(net_addr_comp(&pBan->m_Info.m_Addr, &BanAddr) == 0)
				break;
		}
		if(pBan)
		{
			char BanStr[128];
			if(pBan->m_Info.m_Expires > -1)
			{
				int Mins = ((pBan->m_Info.m_Expires - Now)+59)/60;
				if(Mins <= 1)
					str_format(BanStr, sizeof(BanStr), "Banned for 1 minute (%s)", pBan->m_Info.m_Reason);
				else
					str_format(BanStr, sizeof(BanStr), "Banned for %d minutes (%s)", Mins, pBan->m_Info.m_Reason);
			}
			else
				str_format(BanStr, sizeof(BanStr), "Banned for life (%s)", pBan->m_Info.m_Reason);
			CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CLOSE, BanStr, str_length(BanStr)+1, Token, Sixup);
			continue;
		}

		if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONNLESS)
		{
			if(Sixup && Token != GetToken(Addr) && Token != GetGlobalToken())
				continue;

			pChunk->m_Flags = NETSENDFLAG_CONNLESS;
			pChunk->m_ClientID = -1;
			pChunk->m_Address = Addr;
			pChunk->m_DataSize = m_RecvUnpacker.m_Data.m_DataSize;
			pChunk->m_pData = m_RecvUnpacker.m_Data.m_aChunkData;
			return 1;
		}

		if(m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_CONTROL &&
			m_RecvUnpacker.m_Data.m_DataSize == 0)
			continue;

		int Slot = GetClientSlot(Addr);
		if(!Sixup && Slot != -1 && m_aSlots[Slot].m_Connection.m_Sixup)
		{
			Sixup = true;
			if(CNetBase::UnpackPacket(m_RecvUnpacker.m_aBuffer, Bytes, &m_RecvUnpacker.m_Data, Sixup, &Token, &ResponseToken) != 0)
				continue;
		}

		if(Slot != -1)
		{
			if(m_aSlots[Slot].m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr, Token))
			{
				if(m_RecvUnpacker.m_Data.m_DataSize)
					m_RecvUnpacker.Start(&Addr, &m_aSlots[Slot].m_Connection, Slot);
			}
		}
		else if(Sixup)
		{
			if(OnSixupCtrlMsg(Addr, pChunk, m_RecvUnpacker.m_Data.m_aChunkData[0], m_RecvUnpacker.m_Data, ResponseToken, Token) == 1)
				return 1;
		}
		else if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONTROL &&
			m_RecvUnpacker.m_Data.m_aChunkData[0] == NET_CTRLMSG_CONNECT)
		{
			// classic 0.6 connect (no token)
			CNetBase::SendControlMsg(m_Socket, &Addr, 0, NET_CTRLMSG_CONNECTACCEPT, 0, 0, NET_SECURITY_TOKEN_UNSUPPORTED, false);
			TryAcceptClient(Addr, NET_SECURITY_TOKEN_UNSUPPORTED, false, false, 0);
		}
	}
	return 0;
}

int CNetServer::Send(CNetChunk *pChunk)
{
	if(pChunk->m_DataSize >= NET_MAX_PAYLOAD)
	{
		dbg_msg("netserver", "packet payload too big. %d. dropping packet", pChunk->m_DataSize);
		return -1;
	}

	if(pChunk->m_Flags&NETSENDFLAG_CONNLESS)
	{
		// send connectionless packet
		CNetBase::SendPacketConnless(m_Socket, &pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize);
	}
	else
	{
		int Flags = 0;
		dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
		dbg_assert(pChunk->m_ClientID < MaxClients(), "errornous client id");

		if(pChunk->m_Flags&NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		if(m_aSlots[pChunk->m_ClientID].m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData) == 0)
		{
			if(pChunk->m_Flags&NETSENDFLAG_FLUSH)
				m_aSlots[pChunk->m_ClientID].m_Connection.Flush();
		}
		else
		{
			// QueueChunkEx Disconnect'd on buffer full. Drop cleans game state.
			// Re-entrant sends during OnClientDrop hit m_Dropping / offline QueueChunk.
			Drop(pChunk->m_ClientID, "Error sending data");
		}
	}
	return 0;
}

void CNetServer::SetMaxClientsPerIP(int Max)
{
	// clamp
	if(Max < 1)
		Max = 1;
	else if(Max > NET_MAX_CLIENTS)
		Max = NET_MAX_CLIENTS;

	m_MaxClientsPerIP = Max;
}
