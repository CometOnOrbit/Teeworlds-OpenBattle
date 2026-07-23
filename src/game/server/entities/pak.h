#ifndef GAME_SERVER_ENTITIES_PAK_H
#define GAME_SERVER_ENTITIES_PAK_H

#include <game/server/entity.h>

class CCharacter;

class CPak : public CEntity
{
public:
	enum
	{
		MODE_TARGETER = 1,
		MODE_GUIDED_MISSILE = 2,
		MODE_REMOTE_GRENADE = 3,
	};

	CPak(CGameWorld *pGameWorld, int Owner, int Mode, vec2 Pos = vec2(0, 0), int TargetCID = -1);
	virtual ~CPak();

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void TickTargeter(CCharacter *pOwner);
	void TickGuidedMissile(CCharacter *pOwner);
	void TickRemoteGrenade(CCharacter *pOwner);
	void ExplodeSingle();
	void ExplodeRemoteAxis(bool HorizontalCollision);
	void SnapProjectile(int ID, vec2 Pos, vec2 Vel, int Type);

	int m_Owner;
	int m_Mode;
	int m_TargetCID;
	int m_LifeTimer;
	int m_AccelerationTimer;
	int m_LockTimer;
	bool m_SignalLost;
	int m_SampleToggle;
	vec2 m_SamplePos;
	vec2 m_Direction;
	vec2 m_Vel;
	CCharacter *m_pLockTarget;
};

#endif
