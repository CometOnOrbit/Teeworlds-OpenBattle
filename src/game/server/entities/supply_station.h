#ifndef GAME_SERVER_ENTITIES_SUPPLY_STATION_H
#define GAME_SERVER_ENTITIES_SUPPLY_STATION_H

#include <game/server/entity.h>

class CSupplyStation : public CEntity
{
public:
	CSupplyStation(CGameWorld *pGameWorld, vec2 Pos, int Type);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	void Hit();
	void FireHealthShots();
	void SnapPickup(int Type, int Subtype);
	void SnapLaser(int ID, vec2 From, vec2 To);

	vec2 m_Vel;
	int m_AmmoCooldown;
	int m_Type;
	int m_AnimTimer;
	int m_NumHealthShots;
	int m_HealthDowntime;
	int m_FirstHealthCooldown;
	int m_SecondHealthCooldown;
	vec2 m_CurrentPos;
	int m_SnapClient;
};

#endif
