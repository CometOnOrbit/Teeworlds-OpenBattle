/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_H
#define ENGINE_SERVER_H
#include "kernel.h"
#include "message.h"

#include <type_traits>

#include <game/generated/protocol.h>
#include <game/generated/protocol7.h>

class IServer : public IInterface
{
	MACRO_INTERFACE("server", 0)
protected:
	int m_CurrentGameTick;
	int m_TickSpeed;

public:
	/*
		Structure: CClientInfo
	*/
	struct CClientInfo
	{
		const char *m_pName;
		int m_Latency;
	};

	int Tick() const { return m_CurrentGameTick; }
	int TickSpeed() const { return m_TickSpeed; }

	virtual const char *ClientName(int ClientID) = 0;
	virtual const char *ClientClan(int ClientID) = 0;
	virtual int ClientCountry(int ClientID) = 0;
	virtual bool ClientIngame(int ClientID) = 0;
	virtual int GetClientInfo(int ClientID, CClientInfo *pInfo) = 0;
	virtual void GetClientAddr(int ClientID, char *pAddrStr, int Size) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID) = 0;

	template<class T>
	int SendPackMsgOne(T *pMsg, int Flags, int ClientID)
	{
		CMsgPacker Packer(pMsg->MsgID(), protocol7::is_sixup<T>::value);
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags, ClientID);
	}

	template<class T>
	int SendPackMsgTranslate(T *pMsg, int Flags, int ClientID)
	{
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	int SendPackMsgTranslate(CNetMsg_Sv_Chat *pMsg, int Flags, int ClientID)
	{
		if(IsSixup(ClientID))
		{
			protocol7::CNetMsg_Sv_Chat Msg7;
			Msg7.m_ClientID = pMsg->m_ClientID;
			Msg7.m_pMessage = pMsg->m_pMessage;
			Msg7.m_Mode = pMsg->m_Team > 0 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
			Msg7.m_TargetID = -1;
			return SendPackMsgOne(&Msg7, Flags, ClientID);
		}
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	template<class T, typename std::enable_if<!protocol7::is_sixup<T>::value, int>::type = 0>
	int SendPackMsg(T *pMsg, int Flags, int ClientID)
	{
		int Result = 0;
		if(ClientID == -1)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!ClientIngame(i))
					continue;
				Result = SendPackMsgTranslate(pMsg, Flags, i);
			}
		}
		else
			Result = SendPackMsgTranslate(pMsg, Flags, ClientID);
		return Result;
	}

	template<class T, typename std::enable_if<protocol7::is_sixup<T>::value, int>::type = 1>
	int SendPackMsg(T *pMsg, int Flags, int ClientID)
	{
		int Result = 0;
		if(ClientID == -1)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(ClientIngame(i) && IsSixup(i))
					Result = SendPackMsgOne(pMsg, Flags, i);
			}
		}
		else if(IsSixup(ClientID))
			Result = SendPackMsgOne(pMsg, Flags, ClientID);
		return Result;
	}

	virtual void SetClientName(int ClientID, char const *pName) = 0;
	virtual void SetClientClan(int ClientID, char const *pClan) = 0;
	virtual void SetClientCountry(int ClientID, int Country) = 0;
	virtual void SetClientScore(int ClientID, int Score) = 0;

	virtual int SnapNewID() = 0;
	virtual void SnapFreeID(int ID) = 0;
	virtual void *SnapNewItem(int Type, int ID, int Size) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	virtual bool IsAuthed(int ClientID) = 0;
	virtual void Kick(int ClientID, const char *pReason) = 0;
	virtual bool IsSixup(int ClientID) const { return false; }

	virtual void DemoRecorder_HandleAutoStart() = 0;
};

class IGameServer : public IInterface
{
	MACRO_INTERFACE("gameserver", 0)
protected:
public:
	virtual void OnInit() = 0;
	virtual void OnConsoleInit() = 0;
	virtual void OnShutdown() = 0;

	virtual void OnTick() = 0;
	virtual void OnPreSnap() = 0;
	virtual void OnSnap(int ClientID) = 0;
	virtual void OnPostSnap() = 0;

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID) = 0;

	virtual void OnClientConnected(int ClientID) = 0;
	virtual void OnClientEnter(int ClientID) = 0;
	virtual void OnClientDrop(int ClientID, const char *pReason) = 0;
	virtual void OnClientDirectInput(int ClientID, void *pInput) = 0;
	virtual void OnClientPredictedInput(int ClientID, void *pInput) = 0;

	virtual bool IsClientReady(int ClientID) = 0;
	virtual bool IsClientPlayer(int ClientID) = 0;

	virtual const char *GameType() = 0;
	virtual const char *Version() = 0;
	virtual const char *NetVersion() = 0;
};

extern IGameServer *CreateGameServer();
#endif
