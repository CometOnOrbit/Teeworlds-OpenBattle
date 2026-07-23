/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_MESSAGE_H
#define ENGINE_MESSAGE_H

#include <engine/shared/packer.h>

class CMsgPacker : public CPacker
{
public:
	int m_MsgID;
	bool m_NoTranslate;

	CMsgPacker(int Type, bool NoTranslate = false) :
		m_MsgID(Type), m_NoTranslate(NoTranslate)
	{
		Reset();
		AddInt(Type);
	}
};

#endif
