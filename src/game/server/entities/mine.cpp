#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "mine.h"

CMine::CMine(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_CurrentPos = Pos;
	m_ProximityRadius = 25.0f;
	m_Owner = Owner;
	m_BlinkTimer = 5;
	m_Vel = vec2(0, 0);
	m_Defusing = false;
	m_DefuseProgress = 0;
	m_DefuserPos = vec2(0, 0);
	AllocExtraIDs(1);
	GameWorld()->InsertEntity(this);
}

void CMine::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CMine::SnapLaser(int ID, vec2 From, vec2 To)
{
	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(
		Server()->SnapNewItem(NETOBJTYPE_LASER, ID, sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;
	pLaser->m_X = round_to_int(From.x);
	pLaser->m_Y = round_to_int(From.y);
	pLaser->m_FromX = round_to_int(To.x);
	pLaser->m_FromY = round_to_int(To.y);
	pLaser->m_StartTick = Server()->Tick();
}

void CMine::Snap(int SnappingClient)
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pViewer = GameServer()->GetPlayerChar(SnappingClient);
	if(!pOwner || !pViewer)
		return;

	bool SameTeam = pOwner->GetPlayer()->GetTeam() == pViewer->GetPlayer()->GetTeam();
	if(!SameTeam || m_BlinkTimer >= 13)
		SnapLaser(m_ID, m_CurrentPos, m_CurrentPos);
	if(m_Defusing)
		SnapLaser(m_aExtraIDs[0], m_CurrentPos, m_DefuserPos);
}

void CMine::Hit()
{
	if(m_Owner == -1)
		return;
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner)
		return;

	// Enemy engineers defuse a mine by repeatedly producing the one-tick gun
	// action pulse while they have line of sight to it.
	CCharacter *pDefuser = GameWorld()->ClosestCharacter(m_CurrentPos, 200.0f, 0);
	if(pDefuser && pDefuser->IsAlive() &&
		pDefuser->GetPlayer()->GetTeam() != pOwner->GetPlayer()->GetTeam() &&
		pDefuser->GetPlayer()->IsEngineer() && pDefuser->IsEngineerAction() &&
		!GameServer()->Collision()->IntersectLine(m_CurrentPos, pDefuser->m_Pos, 0, 0))
	{
		m_Defusing = true;
		m_DefuserPos = pDefuser->m_Pos;
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), GameServer()->Localize("Defuse Mine: %i | 70", pDefuser->GetPlayer()->GetCID()), m_DefuseProgress);
		GameServer()->SendBroadcast(aBuf, pDefuser->GetPlayer()->GetCID());
		pDefuser->SetBattlefieldBroadcastTimer(20);

		if(m_DefuseProgress >= 70)
		{
			pOwner->ReleaseMineSlot();
			m_Owner = -1;
			GameServer()->CreateExplosion2(m_CurrentPos, pOwner->GetPlayer()->GetCID(), WEAPON_RIFLE, 1);
			GameServer()->CreateSound(m_CurrentPos, SOUND_GRENADE_EXPLODE);
			GameWorld()->DestroyEntity(this);
			return;
		}
	}

	if(m_Owner == -1)
		return;

	CCharacter *pTarget = GameWorld()->ClosestCharacter(m_CurrentPos, 30.0f, 0);
	if(pTarget && pTarget->IsAlive() &&
		pOwner->GetPlayer()->GetTeam() != pTarget->GetPlayer()->GetTeam())
	{
		pOwner->ReleaseMineSlot();
		m_Owner = -1;
		GameServer()->CreateExplosion2(m_CurrentPos, pOwner->GetPlayer()->GetCID(), WEAPON_RIFLE, 1);
		GameServer()->CreateSound(m_CurrentPos, SOUND_GRENADE_EXPLODE);
		// Kill-streak God Mode suppresses the direct mine kill. Slot release,
		// effects and queued destruction still run.
		if(!pTarget->BattlefieldGodModeActive())
			pTarget->Die(pOwner->GetPlayer()->GetCID(), WEAPON_GRENADE);
		GameWorld()->DestroyEntity(this);
	}
}

void CMine::Tick()
{
	if(m_BlinkTimer > 0)
		m_BlinkTimer--;
	if(m_BlinkTimer == 0)
		m_BlinkTimer = 23;

	if(m_Owner == -1)
		return;
	m_Defusing = false;
	if(!GameServer()->GetPlayerChar(m_Owner))
		GameWorld()->DestroyEntity(this);

	if(GameServer()->Collision()->isWater(round_to_int(m_CurrentPos.x), round_to_int(m_CurrentPos.y)))
		m_Vel.y = 3.0f;
	else
		m_Vel.y += GameServer()->Tuning()->m_Gravity;
	if(m_Vel.x > -0.1f && m_Vel.x < 0.1f)
		m_Vel.x = 0.0f;
	GameServer()->Collision()->MoveBox(
		&m_CurrentPos, &m_Vel, vec2(25, 25), 0.5f);
	Hit();

	if(m_Defusing)
		m_DefuseProgress++;
	else if(m_DefuseProgress > 0)
		m_DefuseProgress--;
}
