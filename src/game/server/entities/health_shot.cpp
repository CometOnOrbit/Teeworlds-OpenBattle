#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "health_shot.h"

CHealthShot::CHealthShot(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, int Owner, int Type)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Type = Type;
	m_ProximityRadius = 25.0f;
	m_Owner = Owner;
	m_CurrentPos = Pos;
	m_Direction = Direction;
	m_Vel = vec2(0, 0);

	m_LifeSpan = 0;
	if(m_Type == 2)
		m_LifeSpan = 40;
	else if(m_Type == 1)
		m_LifeSpan = 60;

	m_Friction = 1.1f;
	GameWorld()->InsertEntity(this);
}

void CHealthShot::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CHealthShot::Snap(int SnappingClient)
{
	(void)SnappingClient;
	CNetObj_Pickup *pPickup = static_cast<CNetObj_Pickup *>(
		Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, sizeof(CNetObj_Pickup)));
	if(!pPickup)
		return;

	pPickup->m_X = round_to_int(m_CurrentPos.x);
	pPickup->m_Y = round_to_int(m_CurrentPos.y);
	pPickup->m_Type = POWERUP_HEALTH;
	pPickup->m_Subtype = 0;
}

void CHealthShot::Hit()
{
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_CurrentPos, 30.0f, 0);
	int Collided = GameServer()->Collision()->IntersectLine(
		m_CurrentPos, m_CurrentPos+m_Direction, 0, 0);

	CPlayer *pOwner = m_Owner >= 0 && m_Owner < MAX_CLIENTS ?
		GameServer()->m_apPlayers[m_Owner] : 0;
	if(!pOwner)
		return;

	if(pTarget && pTarget->IsAlive())
	{
		if(m_Type == 1)
		{
			if(pOwner->GetTeam() == pTarget->GetPlayer()->GetTeam())
			{
				if(pTarget->GetHealth() < 10)
				{
					if(pOwnerChar == pTarget)
						return;
					pTarget->IncreaseHealth(2);
					GameServer()->CreateSound(m_CurrentPos, SOUND_PICKUP_HEALTH);
					GameWorld()->DestroyEntity(this);
					if(pOwnerChar)
						pOwnerChar->GetPlayer()->m_Score++;
				}
			}
			else
			{
				pTarget->TakeDamage(vec2(0, 0), 1, m_Owner, WEAPON_HAMMER);
				GameWorld()->DestroyEntity(this);
			}
		}
		else if(pTarget->GetHealth() < 10)
		{
			pTarget->IncreaseHealth(2);
			GameServer()->CreateSound(m_CurrentPos, SOUND_PICKUP_HEALTH);
			GameWorld()->DestroyEntity(this);
		}
	}

	if(Collided && m_Type == 2)
		m_Type = 3;
}

void CHealthShot::Tick()
{
	CPlayer *pOwner = m_Owner >= 0 && m_Owner < MAX_CLIENTS ?
		GameServer()->m_apPlayers[m_Owner] : 0;
	if(!pOwner)
	{
		GameWorld()->DestroyEntity(this);
	}
	else
	{
		CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
		if(!pOwnerChar)
			GameWorld()->DestroyEntity(this);

		if(m_Type == 2)
		{
			float MoveScale = GameServer()->Collision()->isWater(
				round_to_int(m_CurrentPos.x), round_to_int(m_CurrentPos.y)) ? 0.1f : 1.0f;
			m_CurrentPos += m_Direction*MoveScale;
			m_Direction *= m_Friction;
		}
		else if(m_Type == 1)
		{
			float Speed = GameServer()->Collision()->isWater(
				round_to_int(m_CurrentPos.x), round_to_int(m_CurrentPos.y)) ? 3.0f : 20.0f;
			m_Vel = m_Direction*Speed;

			CCharacter *pTarget = GameWorld()->ClosestCharacter(
				m_CurrentPos, 300.0f, pOwnerChar);
			if(pTarget && pTarget->IsAlive() &&
				pOwner->GetTeam() == pTarget->GetPlayer()->GetTeam() &&
				pTarget->GetHealth() < 10)
			{
				m_Vel = normalize(pTarget->m_Pos-m_CurrentPos)*13.0f;
			}

			GameServer()->Collision()->MoveBox(
				&m_CurrentPos, &m_Vel, vec2(14, 14), 0.0f);
		}
	}

	Hit();
	if(m_LifeSpan == 0)
		GameWorld()->DestroyEntity(this);
	if(m_LifeSpan > 0)
		m_LifeSpan--;
}
