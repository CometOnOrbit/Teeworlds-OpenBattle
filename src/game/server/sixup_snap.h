#ifndef GAME_SERVER_SIXUP_SNAP_H
#define GAME_SERVER_SIXUP_SNAP_H

#include <game/generated/protocol.h>
#include <game/generated/protocol7.h>
#include <game/gamecore.h>

// ponytail: 0.7 Pickup has no Subtype — fold POWERUP_WEAPON/NINJA into enum
inline int PickupTypeSeven(int Type, int Subtype)
{
	if(Type == POWERUP_WEAPON)
	{
		switch(Subtype)
		{
		case WEAPON_HAMMER: return protocol7::PICKUP_HAMMER;
		case WEAPON_GUN: return protocol7::PICKUP_GUN;
		case WEAPON_SHOTGUN: return protocol7::PICKUP_SHOTGUN;
		case WEAPON_GRENADE: return protocol7::PICKUP_GRENADE;
		case WEAPON_RIFLE: return protocol7::PICKUP_LASER;
		case WEAPON_NINJA: return protocol7::PICKUP_NINJA;
		default: return protocol7::PICKUP_GUN;
		}
	}
	if(Type == POWERUP_NINJA)
		return protocol7::PICKUP_NINJA;
	return Type; // HEALTH/ARMOR share values with 0.7
}

inline int PickupSnapSize(bool Sixup)
{
	return Sixup ? (int)sizeof(protocol7::CNetObj_Pickup) : (int)sizeof(CNetObj_Pickup);
}

inline int PlayerFlags_SevenToSix(int Flags)
{
	int Six = 0;
	if(Flags & protocol7::PLAYERFLAG_CHATTING)
		Six |= PLAYERFLAG_CHATTING;
	if(Flags & protocol7::PLAYERFLAG_SCOREBOARD)
		Six |= PLAYERFLAG_SCOREBOARD;
	return Six;
}

inline int PlayerFlags_SixToSeven(int Flags)
{
	int Seven = 0;
	if(Flags & PLAYERFLAG_CHATTING)
		Seven |= protocol7::PLAYERFLAG_CHATTING;
	if(Flags & PLAYERFLAG_SCOREBOARD)
		Seven |= protocol7::PLAYERFLAG_SCOREBOARD;
	return Seven;
}

// 0.7 dropped HOOK_LAUNCH/HOOK_RETRACT and shifted attach bits;
// raw 0.6 HOOK_RETRACT/HIT_NOHOOK fail 0.7 Character CheckFlag.
inline int TriggeredEvents_SixToSeven(int Events)
{
	int Seven = 0;
	if(Events & COREEVENT_GROUND_JUMP)
		Seven |= protocol7::COREEVENTFLAG_GROUND_JUMP;
	if(Events & COREEVENT_AIR_JUMP)
		Seven |= protocol7::COREEVENTFLAG_AIR_JUMP;
	if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
		Seven |= protocol7::COREEVENTFLAG_HOOK_ATTACH_PLAYER;
	if(Events & COREEVENT_HOOK_ATTACH_GROUND)
		Seven |= protocol7::COREEVENTFLAG_HOOK_ATTACH_GROUND;
	if(Events & COREEVENT_HOOK_HIT_NOHOOK)
		Seven |= protocol7::COREEVENTFLAG_HOOK_HIT_NOHOOK;
	return Seven;
}

#endif
