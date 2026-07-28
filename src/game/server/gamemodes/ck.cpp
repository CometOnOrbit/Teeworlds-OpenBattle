/* CK: two-leg attack/defence controller. */
#include <engine/shared/config.h>
#include <base/math.h>
#include <base/system.h>
#include <game/mapitems.h>
#include <game/generated/protocol7.h>
#include <game/server/entities/character.h>
#include <game/server/entities/ck_door.h>
#include <game/server/entities/ck_point_flag.h>
#include <game/server/entities/ck_teleport_marker.h>
#include <game/server/entities/flag.h>
#include <game/server/gamecontext.h>
#include <game/server/mapconfig.h>
#include <game/server/player.h>
#include "ck.h"

CGameControllerCK::CGameControllerCK(CGameContext *pGameServer) : IGameController(pGameServer)
{
	m_pGameType = "CK|CTF";
	m_GameFlags = GAMEFLAG_TEAMS|GAMEFLAG_FLAGS;
	m_apFlags[0] = m_apFlags[1] = 0;
	m_AttackingTeam = TEAM_RED;
	m_NodeMask = 0;
	m_GoalMask = 0;
	m_GraphLoaded = false;
	m_MapValid = false;
	m_IntermissionTick = -1;
	m_ResetMatchOnNextRound = false;
	mem_zero(m_aResults, sizeof(m_aResults));
	mem_zero(m_aPointPresent, sizeof(m_aPointPresent));
	mem_zero(m_aaTeleportInPresent, sizeof(m_aaTeleportInPresent));
	mem_zero(m_aaNumTeleportInTiles, sizeof(m_aaNumTeleportInTiles));
	mem_zero(m_aTeleportOutPresent, sizeof(m_aTeleportOutPresent));
	mem_zero(m_aPointFlagPresent, sizeof(m_aPointFlagPresent));
	mem_zero(m_aaPresence, sizeof(m_aaPresence));
	mem_zero(m_apDoors, sizeof(m_apDoors));
	mem_zero(m_aNodes, sizeof(m_aNodes));
	m_NumDoors = 0;
	m_NumDoorTiles = 0;
	m_NumIgnoredLegacyNavigationEntities = 0;
	m_DoorsBuilt = false;
	m_TeleportMarkersBuilt = false;
	ResetCKRound();
}

void CGameControllerCK::Announce(const char *pText)
{
	GameServer()->SendChatTarget(-1, pText);
}

bool CGameControllerCK::IsAt(vec2 A, vec2 B, float Radius) const
{
	return distance(A, B) < Radius;
}

void CGameControllerCK::ResetCKRound()
{
	m_FinalStage = false;
	m_RoundFinished = false;
	m_IntermissionTick = -1;
	mem_zero(m_aaPresence, sizeof(m_aaPresence));
	mem_zero(m_aEmptyTicks, sizeof(m_aEmptyTicks));
	mem_zero(m_aTeleportCooldown, sizeof(m_aTeleportCooldown));
	for(int i = 0; i < MAX_POINTS; ++i)
		m_aPointProgress[i] = 0.0f; // 0 defender-controlled, 1 attacker-controlled
	for(int i = 0; i < 2; ++i)
		if(m_apFlags[i]) m_apFlags[i]->Reset();
}

void CGameControllerCK::StartRound()
{
	// With no sv_maprotation the base controller starts another round on the
	// same instance. A completed CK map must always begin a fresh red leg.
	if(m_ResetMatchOnNextRound)
	{
		m_AttackingTeam = TEAM_RED;
		mem_zero(m_aResults, sizeof(m_aResults));
		m_MapValid = false;
		m_ResetMatchOnNextRound = false;
	}
	// Re-read the sidecar graph for both attack legs. This also prevents a map
	// rotation or a corrected map package from inheriting an earlier graph.
	m_MapValid = false;
	m_GraphLoaded = false;
	IGameController::StartRound();
	ResetCKRound();
}

bool CGameControllerCK::OnEntity(int Index, vec2 Pos)
{
	// DDNet laser-length markers are only consumed together with a numbered
	// Switch-layer Door. Do not let the legacy Battlefield mapping treat them
	// as old door endpoints.
	if(Index >= ENTITY_LASER_SHORT && Index <= ENTITY_LASER_LONG)
		return true;
	// CK doors and teleports come exclusively from numbered Switch/Tele data.
	// Old Battlefield door and checkpoint-line entities otherwise construct
	// CDoor/CCheck instances alongside CK entities. CCheck lines also teleport
	// characters through the old destination array, which looks like a CK door
	// collision and draws a second, full-length laser over the Tele marker.
	if((Index >= ENTITY_BF_DOOR_START_FIRST && Index <= ENTITY_BF_DOOR_START_LAST) ||
		(Index >= ENTITY_BF_DOOR_END_FIRST && Index <= ENTITY_BF_DOOR_END_LAST) ||
		(Index >= ENTITY_BF_SWITCH_FIRST && Index <= ENTITY_BF_SWITCH_LAST) ||
		(Index >= ENTITY_BF_CP_LINE_RED_FIRST && Index <= ENTITY_BF_CP_LINE_RED_LAST) ||
		(Index >= ENTITY_BF_CP_LINE_BLUE_FIRST && Index <= ENTITY_BF_CP_LINE_BLUE_LAST) ||
		(Index >= ENTITY_BF_CP_DEST_FIRST && Index <= ENTITY_BF_CP_DEST_LAST))
	{
		++m_NumIgnoredLegacyNavigationEntities;
		return true;
	}
	if(Index == ENTITY_FLAGSTAND_RED || Index == ENTITY_FLAGSTAND_BLUE)
	{
		int Team = Index == ENTITY_FLAGSTAND_RED ? TEAM_RED : TEAM_BLUE;
		if(m_apFlags[Team]) return true;
		CFlag *pFlag = new CFlag(&GameServer()->m_World, Team);
		pFlag->m_StandPos = pFlag->m_Pos = Pos;
		m_apFlags[Team] = pFlag;
		GameServer()->m_World.InsertEntity(pFlag);
		return true;
	}
	return IGameController::OnEntity(Index, Pos);
}

bool CGameControllerCK::OnSwitchEntity(int Index, vec2 Pos, int Number, int Flags)
{
	(void)Flags;
	if(Index != ENTITY_DOOR && Index != ENTITY_FLAGSTAND_RED && Index != ENTITY_FLAGSTAND_BLUE)
		return false;
	if((Number < 1 || Number > MAX_POINTS) && Number != 255)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", "ignored CK door with invalid Number");
		return true;
	}
	if(Index == ENTITY_DOOR)
	{
		RegisterDoorTile(Pos, Number);
		return true;
	}
	// A Switch-layer flagstand is a CK CP flag anchor. Both editor colours are
	// accepted; the snap colour comes from the point's current controller.
	if((Index == ENTITY_FLAGSTAND_RED || Index == ENTITY_FLAGSTAND_BLUE) && Number >= 1 && Number <= MAX_POINTS)
	{
		new CCKPointFlag(&GameServer()->m_World, Pos, Number);
		m_aPointFlagPresent[Number-1] = true;
		return true;
	}
	return false;
}

void CGameControllerCK::RegisterDoorTile(vec2 Pos, int Number)
{
	if(m_NumDoorTiles >= MAX_DOOR_TILES)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", "ignored CK door tile: map exceeds door tile limit");
		return;
	}
	m_aDoorTilePos[m_NumDoorTiles] = Pos;
	m_aDoorTileNumber[m_NumDoorTiles++] = Number;
}

void CGameControllerCK::BuildDoors()
{
	if(m_DoorsBuilt)
		return;
	m_DoorsBuilt = true;
	bool aConnected[MAX_DOOR_TILES];
	mem_zero(aConnected, sizeof(aConnected));
	const int aaDirections[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
	for(int A = 0; A < m_NumDoorTiles; ++A)
		for(int Direction = 0; Direction < 4; ++Direction)
	{
		int DX = aaDirections[Direction][0];
		int DY = aaDirections[Direction][1];
		auto FindTile = [&](int X, int Y) {
			for(int i = 0; i < m_NumDoorTiles; ++i)
				if(m_aDoorTileNumber[i] == m_aDoorTileNumber[A] &&
					round_to_int(m_aDoorTilePos[i].x) == X && round_to_int(m_aDoorTilePos[i].y) == Y)
					return i;
			return -1;
		};
		int StartX = round_to_int(m_aDoorTilePos[A].x);
		int StartY = round_to_int(m_aDoorTilePos[A].y);
		// Only the first tile of a straight run creates the laser.
		if(FindTile(StartX-DX*32, StartY-DY*32) >= 0)
			continue;
		int End = A;
		int Tiles = 1;
		for(;;)
		{
			int Next = FindTile(round_to_int(m_aDoorTilePos[End].x)+DX*32, round_to_int(m_aDoorTilePos[End].y)+DY*32);
			if(Next < 0)
				break;
			aConnected[End] = aConnected[Next] = true;
			End = Next;
			++Tiles;
		}
		if(Tiles < 2)
			continue;
		if(m_NumDoors >= MAX_DOORS)
		{
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", "ignored CK door runs: map exceeds door limit");
			return;
		}
		// Each tile represents one full 32px brick. Extend the merged centerline
		// by half a tile at both ends so N bricks produce an N-tile-long door.
		vec2 HalfTile(DX*16.0f, DY*16.0f);
		m_apDoors[m_NumDoors++] = new CCKDoor(&GameServer()->m_World,
			m_aDoorTilePos[A]-HalfTile, m_aDoorTilePos[End]+HalfTile, m_aDoorTileNumber[A]);
	}
	for(int i = 0; i < m_NumDoorTiles; ++i)
		if(!aConnected[i])
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", "ignored isolated CK door tile: use at least two adjacent tiles with the same Number");
	char aDoorLog[128];
	str_format(aDoorLog, sizeof(aDoorLog), "CK Door scan tiles=%d segments=%d", m_NumDoorTiles, m_NumDoors);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "ck", aDoorLog);
	for(int Number = 1; Number <= MAX_POINTS; ++Number)
	{
		int Tiles = 0;
		int Segments = 0;
		for(int i = 0; i < m_NumDoorTiles; ++i)
			Tiles += m_aDoorTileNumber[i] == Number ? 1 : 0;
		for(int i = 0; i < m_NumDoors; ++i)
			Segments += m_apDoors[i] && m_apDoors[i]->Number() == Number ? 1 : 0;
		if(Tiles > 0)
		{
			str_format(aDoorLog, sizeof(aDoorLog), "CK Door Number %d tiles=%d segments=%d", Number, Tiles, Segments);
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "ck", aDoorLog);
		}
	}
}

bool CGameControllerCK::IsDoorClosed(int Number) const
{
	if(Number == 255)
		return !m_FinalStage;
	if(Number < 1 || Number > MAX_POINTS)
		return true;
	int Point = Number-1;
	// A numbered Door gates entry to its point. It opens as soon as any graph
	// predecessor makes that point attackable, and remains open after capture.
	return !OwnedByAttackers(Point) && !CanAttackPoint(Point);
}

int CGameControllerCK::PointFlagTeam(int Number) const
{
	int Point = Number-1;
	if(!IsConfigured(Point))
		return -1;
	return OwnedByAttackers(Point) ? m_AttackingTeam : Defender();
}

bool CGameControllerCK::TeleportEnabled(int Number, int Side) const
{
	int Point = Number-1;
	if(Side < 0 || Side > 1)
		return false;
	if(!IsConfigured(Point))
		return false;
	// Unlike the gateway Door, an entrance belongs to the team currently
	// controlling its CP. Exactly one of the two team-specific Tele In tiles
	// can therefore be active at a time.
	return Side == 0 ? OwnedByAttackers(Point) : !OwnedByAttackers(Point);
}

bool CGameControllerCK::DoorBlocksCharacter(int Number, int Team) const
{
	return IsDoorClosed(Number) && Team == m_AttackingTeam;
}

bool CGameControllerCK::CheckpointTileBlocksCharacter(int Number, int Team) const
{
	(void)Number;
	(void)Team;
	// Legacy Battlefield checkpoint tiles can overlap CK's SwitchOpen/Door
	// layout. CKDoor is the sole physical gate in this mode; applying the old
	// tile stop as well snaps characters back each tick and pulls them into the
	// middle of the laser.
	return false;
}

bool CGameControllerCK::IntersectDoor(vec2 From, vec2 To, vec2 *pHit, float Radius) const
{
	for(int i = 0; i < m_NumDoors; ++i)
		if(m_apDoors[i] && IsDoorClosed(m_apDoors[i]->Number()) && m_apDoors[i]->Intersects(From, To, pHit, Radius))
			return true;
	return false;
}

bool CGameControllerCK::CanSpawn(int Team, vec2 *pPos)
{
	if(Team == TEAM_SPECTATORS) return false;
	// The physical red side is always the attack side. Only the team using it
	// changes for leg two; player team colours never change.
	CSpawnEval Eval;
	Eval.m_FriendlyTeam = Team;
	int Physical = Team == m_AttackingTeam ? AttackingPhysicalTeam() : DefendingPhysicalTeam();
	EvaluateSpawnType(&Eval, 1 + Physical);
	if(!Eval.m_Got) EvaluateSpawnType(&Eval, 0);
	if(!Eval.m_Got) EvaluateSpawnType(&Eval, 1 + (Physical ^ 1));
	*pPos = Eval.m_Pos;
	return Eval.m_Got;
}

bool CGameControllerCK::IsConfigured(int Point) const
{
	return Point >= 0 && Point < MAX_POINTS && m_aNodes[Point].m_Configured;
}

bool CGameControllerCK::OwnedByAttackers(int Point) const
{
	return IsConfigured(Point) && m_aPointProgress[Point] >= 1.0f;
}

bool CGameControllerCK::CanAttackPoint(int Point) const
{
	if(!IsConfigured(Point) || OwnedByAttackers(Point))
		return false;
	unsigned short Pred = m_aNodes[Point].m_Predecessors;
	if(!Pred)
		return true;
	for(int i = 0; i < MAX_POINTS; ++i)
		if((Pred&(1<<i)) && OwnedByAttackers(i))
			return true;
	return false;
}

bool CGameControllerCK::CanRetakePoint(int Point) const
{
	if(!OwnedByAttackers(Point))
		return false;
	unsigned short Next = m_aNodes[Point].m_Successors;
	for(int i = 0; i < MAX_POINTS; ++i)
		if((Next&(1<<i)) && !OwnedByAttackers(i))
			return true;
	return false;
}

void CGameControllerCK::SetAttackerClosure(int Point)
{
	if(!IsConfigured(Point))
		return;
	m_aPointProgress[Point] = 1.0f;
	m_aEmptyTicks[Point] = 0;
	unsigned short Pred = m_aNodes[Point].m_Predecessors;
	for(int i = 0; i < MAX_POINTS; ++i)
		if(Pred&(1<<i))
			SetAttackerClosure(i);
}

void CGameControllerCK::SetDefenderClosure(int Point)
{
	if(!IsConfigured(Point))
		return;
	m_aPointProgress[Point] = 0.0f;
	m_aEmptyTicks[Point] = 0;
	unsigned short Next = m_aNodes[Point].m_Successors;
	for(int i = 0; i < MAX_POINTS; ++i)
		if(Next&(1<<i))
			SetDefenderClosure(i);
}

bool CGameControllerCK::AllGoalsCaptured() const
{
	if(!m_GoalMask)
		return false;
	for(int i = 0; i < MAX_POINTS; ++i)
		if((m_GoalMask&(1<<i)) && !OwnedByAttackers(i))
			return false;
	return true;
}

int CGameControllerCK::CapturedPointCount() const
{
	int Count = 0;
	for(int i = 0; i < MAX_POINTS; ++i)
		Count += OwnedByAttackers(i) ? 1 : 0;
	return Count;
}

int CGameControllerCK::AdvanceMetric() const
{
	int BestDepth = -1;
	for(int i = 0; i < MAX_POINTS; ++i)
		if(OwnedByAttackers(i))
			BestDepth = max(BestDepth, m_aNodes[i].m_Depth);
	float Progress = 0.0f;
	for(int i = 0; i < MAX_POINTS; ++i)
		if(CanAttackPoint(i) && m_aNodes[i].m_Depth == BestDepth+1)
			Progress = max(Progress, m_aPointProgress[i]);
	return (BestDepth+1)*1001+round_to_int(Progress*1000.0f);
}

int CGameControllerCK::ActiveCombatants() const
{
	int Count = 0;
	for(int ClientID = 0; ClientID < MAX_CLIENTS; ++ClientID)
	{
		const CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
		if(pPlayer && pPlayer->GetTeam() != TEAM_SPECTATORS)
			++Count;
	}
	return Count;
}

float CGameControllerCK::CaptureDuration() const
{
	// Small games need a quick route to the objective, while large games need
	// enough time for both teams to react. Spectators never affect this value.
	return clamp(12.0f + 0.5f*ActiveCombatants(), 16.0f, 30.0f);
}

float CGameControllerCK::CaptureMultiplier(int PlayersOnPoint) const
{
	// Teamwork matters without allowing a large stack to erase a point
	// instantly: 1 player = 1x, 2 = 1.5x, 3 or more = 2x.
	return 1.0f + 0.5f*clamp(PlayersOnPoint-1, 0, 2);
}

void CGameControllerCK::PointName(int Point, char *pBuf, int BufSize) const
{
	if(IsConfigured(Point))
		str_copy(pBuf, m_aNodes[Point].m_aLabel, BufSize);
	else
		str_copy(pBuf, "?", BufSize);
}

bool CGameControllerCK::ValidateGraph(char *pError, int ErrorSize)
{
	if(!m_NodeMask || !m_GoalMask)
	{
		str_copy(pError, "requires points and goals", ErrorSize);
		return false;
	}
	unsigned short Roots = 0;
	for(int i = 0; i < MAX_POINTS; ++i)
		if(IsConfigured(i) && !m_aNodes[i].m_Predecessors)
			Roots |= 1<<i;
	if(!Roots)
	{
		str_copy(pError, "graph has no root", ErrorSize);
		return false;
	}
	unsigned short Done = 0;
	for(int Pass = 0; Pass < MAX_POINTS; ++Pass)
		for(int i = 0; i < MAX_POINTS; ++i)
			if(IsConfigured(i) && !(Done&(1<<i)) && (m_aNodes[i].m_Predecessors&~Done) == 0)
			{
				int Depth = 0;
				for(int j = 0; j < MAX_POINTS; ++j)
					if(m_aNodes[i].m_Predecessors&(1<<j))
						Depth = max(Depth, m_aNodes[j].m_Depth+1);
				m_aNodes[i].m_Depth = Depth;
				Done |= 1<<i;
			}
	if(Done != m_NodeMask)
	{
		str_copy(pError, "graph contains a cycle", ErrorSize);
		return false;
	}
	unsigned short Reach = Roots;
	for(int Pass = 0; Pass < MAX_POINTS; ++Pass)
		for(int i = 0; i < MAX_POINTS; ++i)
			if(Reach&(1<<i)) Reach |= m_aNodes[i].m_Successors;
	if(Reach != m_NodeMask)
	{
		str_copy(pError, "graph has a node unreachable from a root", ErrorSize);
		return false;
	}
	unsigned short ToGoal = m_GoalMask;
	for(int Pass = 0; Pass < MAX_POINTS; ++Pass)
		for(int i = 0; i < MAX_POINTS; ++i)
			if(ToGoal&(1<<i)) ToGoal |= m_aNodes[i].m_Predecessors;
	if(ToGoal != m_NodeMask)
	{
		str_copy(pError, "graph has a node that cannot reach a goal", ErrorSize);
		return false;
	}
	for(int i = 0; i < MAX_POINTS; ++i)
		if(m_GoalMask&(1<<i))
			if(m_aNodes[i].m_Successors)
			{
				str_copy(pError, "goal must be a terminal node", ErrorSize);
				return false;
			}
	return true;
}

bool CGameControllerCK::LoadGraphConfig(char *pError, int ErrorSize)
{
	CJsonParser Parser;
	json_value *pRoot = 0;
	if(!CMapConfig::LoadForMode(GameServer(), "ck", Parser, &pRoot, pError, ErrorSize))
		return false;
	const json_value &CK = (*pRoot)["ck"];
	const json_value &Points = CK["points"];
	const json_value &Goals = CK["goals"];
	if(CK.type != json_object || Points.type != json_array || Goals.type != json_array)
	{
		str_copy(pError, "CK map config needs ck.points and ck.goals", ErrorSize);
		return false;
	}
	mem_zero(m_aNodes, sizeof(m_aNodes));
	m_NodeMask = m_GoalMask = 0;
	for(unsigned i = 0; i < Points.u.array.length; ++i)
	{
		const json_value &Node = Points[i];
		const json_value &Number = Node["number"];
		const json_value &Label = Node["label"];
		if(Node.type != json_object || Number.type != json_integer || Label.type != json_string || Number.u.integer < 1 || Number.u.integer > MAX_POINTS || !Label.u.string.length)
		{
			str_copy(pError, "invalid point entry", ErrorSize);
			return false;
		}
		int Point = (int)Number.u.integer-1;
		if(m_NodeMask&(1<<Point))
		{
			str_copy(pError, "duplicate point number", ErrorSize);
			return false;
		}
		for(int Other = 0; Other < MAX_POINTS; ++Other)
			if((m_NodeMask&(1<<Other)) && str_comp(m_aNodes[Other].m_aLabel, Label.u.string.ptr) == 0)
			{
				str_copy(pError, "duplicate point label", ErrorSize);
				return false;
			}
		m_NodeMask |= 1<<Point; m_aNodes[Point].m_Configured = true;
		str_copy(m_aNodes[Point].m_aLabel, Label.u.string.ptr, sizeof(m_aNodes[Point].m_aLabel));
	}
	for(unsigned i = 0; i < Points.u.array.length; ++i)
	{
		const json_value &Node = Points[i];
		int Point = (int)Node["number"].u.integer-1;
		const json_value &From = Node["from"];
		if(From.type != json_array)
		{
			str_copy(pError, "each point needs a from array", ErrorSize);
			return false;
		}
		for(unsigned j = 0; j < From.u.array.length; ++j)
		{
			if(From[j].type != json_integer || From[j].u.integer < 1 || From[j].u.integer > MAX_POINTS)
			{
				str_copy(pError, "invalid predecessor", ErrorSize);
				return false;
			}
			int Pred = (int)From[j].u.integer-1;
			if(Pred == Point || !(m_NodeMask&(1<<Pred)))
			{
				str_copy(pError, "unknown predecessor", ErrorSize);
				return false;
			}
			m_aNodes[Point].m_Predecessors |= 1<<Pred;
			m_aNodes[Pred].m_Successors |= 1<<Point;
		}
	}
	for(unsigned i = 0; i < Goals.u.array.length; ++i)
	{
		int Goal = Goals[i].type == json_integer ? (int)Goals[i].u.integer-1 : -1;
		if(Goal < 0 || Goal >= MAX_POINTS || !(m_NodeMask&(1<<Goal)))
		{
			str_copy(pError, "invalid goal", ErrorSize);
			return false;
		}
		m_GoalMask |= 1<<Goal;
	}
	if(!ValidateGraph(pError, ErrorSize))
		return false;
	m_GraphLoaded = true;
	return true;
}

void CGameControllerCK::ScanMap()
{
	if(m_MapValid || m_RoundFinished) return;
	CCollision *pCollision = GameServer()->Collision();
	CMapItemLayerTilemap *pSwitch = GameServer()->Layers()->SwitchLayer();
	int SwitchData = GameServer()->Layers()->SwitchData();
	int SwitchDataSize = SwitchData >= 0 ? GameServer()->Layers()->Map()->GetDataSize(SwitchData) : 0;
	int NumSwitchObjectives = 0;
	int aSwitchTypes[6] = {0, 0, 0, 0, 0, 0};
	if(pSwitch && SwitchData >= 0 && pSwitch->m_Width > 0 && pSwitch->m_Height > 0 &&
		pSwitch->m_Width == pCollision->GetWidth() && pSwitch->m_Height == pCollision->GetHeight())
	{
		CSwitchTile *pTiles = static_cast<CSwitchTile *>(GameServer()->Layers()->Map()->GetData(SwitchData));
		if(pTiles)
			for(int i = 0; i < pSwitch->m_Width*pSwitch->m_Height; ++i)
			{
				if(pTiles[i].m_Type >= 22 && pTiles[i].m_Type <= 27)
					++aSwitchTypes[pTiles[i].m_Type-22];
				if(pTiles[i].m_Type == TILE_SWITCHOPEN && pTiles[i].m_Number >= 1 && pTiles[i].m_Number <= MAX_POINTS)
				{
					int Point = pTiles[i].m_Number-1;
					m_aPointPresent[Point] = true;
					m_aPointPos[Point] = vec2((i%pSwitch->m_Width)*32.0f+16.0f, (i/pSwitch->m_Width)*32.0f+16.0f);
					++NumSwitchObjectives;
				}
			}
	}
	char aSwitchLog[192];
	int SwitchVersion = pSwitch ? pSwitch->m_Version : -1;
	int SwitchFlags = pSwitch ? pSwitch->m_Flags : -1;
	int SwitchRawData = pSwitch ? pSwitch->m_Data : -1;
	str_format(aSwitchLog, sizeof(aSwitchLog), "Switch scan %dx%d version=%d flags=%d data=%d resolved=%d size=%d types22-27=%d/%d/%d/%d/%d/%d objectives=%d", pSwitch ? pSwitch->m_Width : 0, pSwitch ? pSwitch->m_Height : 0, SwitchVersion, SwitchFlags, SwitchRawData, SwitchData, SwitchDataSize, aSwitchTypes[0], aSwitchTypes[1], aSwitchTypes[2], aSwitchTypes[3], aSwitchTypes[4], aSwitchTypes[5], NumSwitchObjectives);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "ck", aSwitchLog);
	char aError[256]; mem_zero(aError, sizeof(aError));
	if(!LoadGraphConfig(aError, sizeof(aError)))
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", aError);
		FinishRound("invalid CK graph", false);
		return;
	}
	BuildDoors();
	if(m_NumIgnoredLegacyNavigationEntities > 0)
	{
		char aLegacyLog[128];
		str_format(aLegacyLog, sizeof(aLegacyLog), "ignored %d legacy door/checkpoint navigation entities in CK", m_NumIgnoredLegacyNavigationEntities);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "ck", aLegacyLog);
	}
	// Tele In Numbers 1-16 belong to attackers and 17-32 to defenders. Tele
	// Out uses the shared CP Number 1-16, so both sides arrive at one endpoint.
	CMapItemLayerTilemap *pTele = GameServer()->Layers()->TeleLayer();
	int TeleData = GameServer()->Layers()->TeleData();
	int NumTeleIn = 0;
	int NumTeleOut = 0;
	if(TeleData >= 0 && pCollision->GetWidth() > 0 && pCollision->GetHeight() > 0)
	{
		CTeleTile *pTiles = static_cast<CTeleTile *>(GameServer()->Layers()->Map()->GetData(TeleData));
		if(pTiles)
			for(int i = 0; i < pCollision->GetWidth()*pCollision->GetHeight(); ++i)
			{
				vec2 Pos((i%pCollision->GetWidth())*32.0f+16.0f, (i/pCollision->GetWidth())*32.0f+16.0f);
				if(pTiles[i].m_Type == TILE_TELEIN && pTiles[i].m_Number >= 1 && pTiles[i].m_Number <= 2*MAX_POINTS)
				{
					int Side = (pTiles[i].m_Number-1)/MAX_POINTS;
					int Number = (pTiles[i].m_Number-1)%MAX_POINTS;
					if(!m_aaTeleportInPresent[Side][Number])
					{
						++NumTeleIn;
						m_aaTeleportInPresent[Side][Number] = true;
					}
					if(!m_TeleportMarkersBuilt && m_aaNumTeleportInTiles[Side][Number] < MAX_TELEPORT_IN_TILES)
						m_aaaTeleportIn[Side][Number][m_aaNumTeleportInTiles[Side][Number]++] = Pos;
				}
				else if(pTiles[i].m_Type == TILE_TELEOUT && pTiles[i].m_Number >= 1 && pTiles[i].m_Number <= MAX_POINTS)
				{
					int Number = pTiles[i].m_Number-1;
					if(!m_aTeleportOutPresent[Number])
					{
						++NumTeleOut;
						m_aTeleportOut[Number] = Pos;
						m_aTeleportOutPresent[Number] = true;
					}
				}
		}
	}
	if(!m_TeleportMarkersBuilt)
	{
		m_TeleportMarkersBuilt = true;
		for(int Side = 0; Side < 2; ++Side)
			for(int Number = 0; Number < MAX_POINTS; ++Number)
			{
				int Count = m_aaNumTeleportInTiles[Side][Number];
				if(Count <= 0)
					continue;
				vec2 From = m_aaaTeleportIn[Side][Number][0];
				vec2 To = From;
				float Longest = 0.0f;
				for(int A = 0; A < Count; ++A)
					for(int B = A+1; B < Count; ++B)
					{
						float Length = distance(m_aaaTeleportIn[Side][Number][A], m_aaaTeleportIn[Side][Number][B]);
						if(Length > Longest)
						{
							Longest = Length;
							From = m_aaaTeleportIn[Side][Number][A];
							To = m_aaaTeleportIn[Side][Number][B];
						}
					}
				if(Longest < 0.001f)
				{
					From.x -= 16.0f;
					To.x += 16.0f;
				}
				else
				{
					vec2 Direction = To-From;
					Direction *= 1.0f/Longest;
					float Extension = 16.0f/max(absolute(Direction.x), absolute(Direction.y));
					From -= Direction*Extension;
					To += Direction*Extension;
				}
				new CCKTeleportMarker(&GameServer()->m_World, From, To, Number+1, Side);
			}
	}
	char aTeleLog[192];
	str_format(aTeleLog, sizeof(aTeleLog), "Tele scan %dx%d version=%d flags=%d data=%d resolved=%d In=%d Out=%d", pTele ? pTele->m_Width : 0, pTele ? pTele->m_Height : 0, pTele ? pTele->m_Version : -1, pTele ? pTele->m_Flags : -1, pTele ? pTele->m_Data : -1, TeleData, NumTeleIn, NumTeleOut);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "ck", aTeleLog);
	m_MapValid = m_apFlags[TEAM_RED] && m_apFlags[TEAM_BLUE] &&
		m_aNumSpawnPoints[1] > 0 && m_aNumSpawnPoints[2] > 0;
	for(int i = 0; i < MAX_POINTS; ++i)
		if(IsConfigured(i) != m_aPointPresent[i])
		{
			m_MapValid = false;
			str_format(aError, sizeof(aError), "Switch objective mismatch for Number %d", i+1);
			break;
		}
	for(int i = 0; i < m_NumDoors; ++i)
		if(m_apDoors[i] && m_apDoors[i]->Number() != 255 && !IsConfigured(m_apDoors[i]->Number()-1))
		{
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", "CK door has no matching graph point");
			m_MapValid = false;
		}
	for(int i = 0; i < MAX_POINTS; ++i)
	{
		if(m_aTeleportOutPresent[i] && !IsConfigured(i))
			{
				char aTeleportError[128];
				str_format(aTeleportError, sizeof(aTeleportError), "invalid CK Tele Out Number %d (no matching graph point)", i+1);
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", aTeleportError);
				m_MapValid = false;
			}
		for(int Side = 0; Side < 2; ++Side)
			if(m_aaTeleportInPresent[Side][i] && (!IsConfigured(i) || !m_aTeleportOutPresent[i]))
			{
				char aTeleportError[128];
				str_format(aTeleportError, sizeof(aTeleportError), "invalid CK Tele In Number %d (%s, %s)",
					i+1, Side == 0 ? "attacker" : "defender", !IsConfigured(i) ? "no matching graph point" : "missing shared Tele Out");
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", aTeleportError);
				m_MapValid = false;
			}
	}
	for(int i = 0; i < MAX_POINTS; ++i)
		if(m_aPointFlagPresent[i] && !IsConfigured(i))
		{
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", "CK point flag has no matching graph point");
			m_MapValid = false;
		}
	if(!m_MapValid)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", aError[0] ? aError : "invalid CK map: requires red/blue spawns, flags, matching Doors and Teleports");
		FinishRound("invalid map", false);
	}
	else
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "ck", "CK map validated");
}

void CGameControllerCK::RegisterCheckpointPresence(int Point, int ClientID)
{
	if(Point < 0 || Point >= MAX_POINTS || ClientID < 0 || ClientID >= MAX_CLIENTS) return;
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	if(!pPlayer || !pPlayer->GetCharacter() || !pPlayer->GetCharacter()->IsAlive() || pPlayer->GetTeam() == TEAM_SPECTATORS) return;
	if(!IsConfigured(Point)) return;
	m_aaPresence[Point][ClientID] = true;
}

void CGameControllerCK::SyncLegacyCheckpointProgress()
{
	// Existing battle maps render their three CP bars from this legacy state:
	// -400 is red and +400 is blue. CK keeps its own normalized, side-relative
	// progress, so mirror CP 1-3 for those visual entities only.
	for(int Point = 0; Point < 3; ++Point)
	{
		if(!IsConfigured(Point))
		{
			GameServer()->m_aCheckpointState[Point] = 0;
			continue;
		}
		float Progress = m_aPointProgress[Point];
		float State = m_AttackingTeam == TEAM_RED ? 400.0f-Progress*800.0f : -400.0f+Progress*800.0f;
		GameServer()->m_aCheckpointState[Point] = round_to_int(State);
	}
}

void CGameControllerCK::TickPoints()
{
	const float Step = 1.0f/(CaptureDuration()*Server()->TickSpeed());
	int aAttack[MAX_POINTS]; int aDefend[MAX_POINTS]; int aFirst[MAX_POINTS];
	mem_zero(aAttack, sizeof(aAttack)); mem_zero(aDefend, sizeof(aDefend));
	for(int Point = 0; Point < MAX_POINTS; ++Point) aFirst[Point] = -1;

	// A Switch objective can cover any number of tiles. Looking only at the
	// last tile found while loading made all other cells in the capture area
	// inert. Determine the CP from each character's current Switch cell.
	CMapItemLayerTilemap *pSwitch = GameServer()->Layers()->SwitchLayer();
	int SwitchData = GameServer()->Layers()->SwitchData();
	CSwitchTile *pSwitchTiles = pSwitch && SwitchData >= 0 &&
		pSwitch->m_Width == GameServer()->Collision()->GetWidth() &&
		pSwitch->m_Height == GameServer()->Collision()->GetHeight() ?
		static_cast<CSwitchTile *>(GameServer()->Layers()->Map()->GetData(SwitchData)) : 0;
	if(pSwitchTiles)
	{
		for(int ID = 0; ID < MAX_CLIENTS; ++ID)
		{
			CPlayer *p = GameServer()->m_apPlayers[ID];
			if(!p || !p->GetCharacter() || !p->GetCharacter()->IsAlive() || p->GetTeam() == TEAM_SPECTATORS)
				continue;
			int X = clamp((int)(p->GetCharacter()->m_Pos.x/32.0f), 0, pSwitch->m_Width-1);
			int Y = clamp((int)(p->GetCharacter()->m_Pos.y/32.0f), 0, pSwitch->m_Height-1);
			const CSwitchTile &Tile = pSwitchTiles[Y*pSwitch->m_Width+X];
			if(Tile.m_Type == TILE_SWITCHOPEN && Tile.m_Number >= 1 && Tile.m_Number <= MAX_POINTS && IsConfigured(Tile.m_Number-1))
				m_aaPresence[Tile.m_Number-1][ID] = true;
		}
	}
	for(int Point = 0; Point < MAX_POINTS; ++Point)
	{
		if(!IsConfigured(Point)) continue;
		for(int ID = 0; ID < MAX_CLIENTS; ++ID)
		{
			if(!m_aaPresence[Point][ID]) continue;
			CPlayer *p = GameServer()->m_apPlayers[ID];
			if(p && p->GetCharacter() && p->GetCharacter()->IsAlive() && p->GetTeam() != TEAM_SPECTATORS)
			{
				if(p->GetTeam() == m_AttackingTeam) ++aAttack[Point]; else ++aDefend[Point];
				if(aFirst[Point] < 0) aFirst[Point] = ID;
			}
			m_aaPresence[Point][ID] = false;
		}
	}
	// Apply defender completions first so a simultaneous retake invalidates an
	// attack which would otherwise cross the newly restored line.
	for(int Point = 0; Point < MAX_POINTS; ++Point)
		if(CanRetakePoint(Point) && aDefend[Point] && !aAttack[Point])
		{
			m_aPointProgress[Point] = max(0.0f, m_aPointProgress[Point]-Step*CaptureMultiplier(aDefend[Point]));
			if(m_aPointProgress[Point] <= 0.0f)
			{
				char aName[32], aBuf[96]; PointName(Point, aName, sizeof(aName));
				SetDefenderClosure(Point);
				str_format(aBuf, sizeof(aBuf), "Defenders retook %s.", aName); Announce(aBuf);
			}
		}
	for(int Point = 0; Point < MAX_POINTS; ++Point)
		if(CanAttackPoint(Point) && aAttack[Point] && !aDefend[Point])
		{
			m_aPointProgress[Point] = min(1.0f, m_aPointProgress[Point]+Step*CaptureMultiplier(aAttack[Point]));
			if(m_aPointProgress[Point] >= 1.0f)
			{
				if(aFirst[Point] >= 0 && GameServer()->m_apPlayers[aFirst[Point]]) GameServer()->m_apPlayers[aFirst[Point]]->m_Score += 5;
				char aName[32], aBuf[96]; PointName(Point, aName, sizeof(aName));
				SetAttackerClosure(Point);
				str_format(aBuf, sizeof(aBuf), "Attackers captured %s.", aName); Announce(aBuf);
			}
		}
	for(int Point = 0; Point < MAX_POINTS; ++Point)
		if(IsConfigured(Point) && !aAttack[Point] && !aDefend[Point] && ++m_aEmptyTicks[Point] > 3*Server()->TickSpeed())
		{
			if(CanAttackPoint(Point)) m_aPointProgress[Point] = max(0.0f, m_aPointProgress[Point]-Step*0.5f);
			else if(CanRetakePoint(Point)) m_aPointProgress[Point] = min(1.0f, m_aPointProgress[Point]+Step*0.5f);
		}
		else if(IsConfigured(Point)) m_aEmptyTicks[Point] = 0;
	if(!m_FinalStage && AllGoalsCaptured()) { m_FinalStage = true; Announce("Final stage: the defending flag can now be taken."); }
}

void CGameControllerCK::TickTeleports()
{
	for(int ClientID = 0; ClientID < MAX_CLIENTS; ++ClientID)
	{
		if(m_aTeleportCooldown[ClientID] > 0)
			--m_aTeleportCooldown[ClientID];
		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
		CCharacter *pCharacter = pPlayer ? pPlayer->GetCharacter() : 0;
		if(!pCharacter || !pCharacter->IsAlive() || pPlayer->GetTeam() == TEAM_SPECTATORS ||
			pCharacter->InBattlefieldVehicle() || m_aTeleportCooldown[ClientID] > 0)
			continue;
		int Side = pPlayer->GetTeam() == m_AttackingTeam ? 0 : 1;
		for(int Number = 0; Number < MAX_POINTS; ++Number)
		{
			bool AtEntrance = false;
			for(int Tile = 0; Tile < m_aaNumTeleportInTiles[Side][Number]; ++Tile)
				if(IsAt(pCharacter->m_Pos, m_aaaTeleportIn[Side][Number][Tile], 24.0f))
				{
					AtEntrance = true;
					break;
				}
			if(TeleportEnabled(Number+1, Side) && AtEntrance && m_aTeleportOutPresent[Number])
			{
				pCharacter->Tele(m_aTeleportOut[Number]);
				m_aTeleportCooldown[ClientID] = Server()->TickSpeed()/2;
				GameServer()->CreateSound(m_aTeleportOut[Number], SOUND_CTF_GRAB_PL);
				break;
			}
		}
	}
}

int CGameControllerCK::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	int Result = IGameController::OnCharacterDeath(pVictim, pKiller, Weapon);
	for(int i = 0; i < 2; ++i)
		if(m_apFlags[i] && m_apFlags[i]->m_pCarryingCharacter == pVictim)
		{
			m_apFlags[i]->m_pCarryingCharacter = 0; m_apFlags[i]->m_AtStand = 0;
			m_apFlags[i]->m_DropTick = Server()->Tick(); m_apFlags[i]->m_Vel = vec2(); Result |= 1;
		}
	return Result;
}

bool CGameControllerCK::CanBeMovedOnBalance(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !GameServer()->m_apPlayers[ClientID])
		return true;
	CCharacter *p = GameServer()->m_apPlayers[ClientID]->GetCharacter();
	return !p || !m_apFlags[DefendingPhysicalTeam()] || m_apFlags[DefendingPhysicalTeam()]->m_pCarryingCharacter != p;
}

void CGameControllerCK::TickFlags()
{
	for(int i = 0; i < 2; ++i)
	{
		CFlag *F = m_apFlags[i]; if(!F) continue;
		if(F->m_pCarryingCharacter)
		{
			F->m_Pos = F->m_pCarryingCharacter->m_Pos;
			if(i == DefendingPhysicalTeam() && IsAt(F->m_Pos, m_apFlags[AttackingPhysicalTeam()]->m_Pos, CFlag::ms_PhysSize+CCharacter::ms_PhysSize))
				FinishRound("defending flag delivered", true);
			continue;
		}
		if((GameServer()->Collision()->GetCollisionAt(F->m_Pos.x,F->m_Pos.y)&CCollision::COLFLAG_DEATH) || F->GameLayerClipped(F->m_Pos)) { F->Reset(); continue; }
		CCharacter *aChars[MAX_CLIENTS];
		int Num = GameServer()->m_World.FindEntities(F->m_Pos, CFlag::ms_PhysSize, (CEntity **)aChars, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int c = 0; c < Num; ++c)
		{
			CCharacter *p = aChars[c]; if(!p->IsAlive() || p->GetPlayer()->GetTeam() == TEAM_SPECTATORS) continue;
			if(i == AttackingPhysicalTeam()) continue; // physical attack flag is a base, never portable
			if(p->GetPlayer()->GetTeam() == Defender() && !F->m_AtStand) { F->Reset(); break; }
			if(p->GetPlayer()->GetTeam() == m_AttackingTeam && m_FinalStage)
			{ F->m_AtStand = 0; F->m_GrabTick = Server()->Tick(); F->m_pCarryingCharacter = p; break; }
		}
		if(!F->m_pCarryingCharacter && !F->m_AtStand)
		{
			if(Server()->Tick() > F->m_DropTick+30*Server()->TickSpeed()) F->Reset();
			else { F->m_Vel.y += GameServer()->m_World.m_Core.m_Tuning.m_Gravity; GameServer()->Collision()->MoveBox(&F->m_Pos, &F->m_Vel, vec2(F->ms_PhysSize,F->ms_PhysSize), .5f); }
		}
	}
}

void CGameControllerCK::FinishRound(const char *pReason, bool Completed)
{
	if(m_RoundFinished) return;
	m_RoundFinished = true;
	CRoundResult &R = m_aResults[m_AttackingTeam];
	R.m_Completed = Completed; R.m_TimeTicks = Server()->Tick()-m_RoundStartTick;
	R.m_Points = CapturedPointCount(); R.m_Progress = AdvanceMetric();
	char aBuf[192]; str_format(aBuf, sizeof(aBuf), "%s attack round ended: %s.", m_AttackingTeam == TEAM_RED ? "Red" : "Blue", pReason); Announce(aBuf);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ck", aBuf);
	GameServer()->m_World.m_Paused = true; m_IntermissionTick = Server()->Tick();
}

void CGameControllerCK::AnnounceMapResult()
{
	const CRoundResult &Red=m_aResults[TEAM_RED], &Blue=m_aResults[TEAM_BLUE]; int Winner=-1;
	if(Red.m_Completed != Blue.m_Completed) Winner=Red.m_Completed ? TEAM_RED : TEAM_BLUE;
	else if(Red.m_Completed && Red.m_TimeTicks != Blue.m_TimeTicks) Winner=Red.m_TimeTicks < Blue.m_TimeTicks ? TEAM_RED : TEAM_BLUE;
	else if(Red.m_Points != Blue.m_Points) Winner=Red.m_Points > Blue.m_Points ? TEAM_RED : TEAM_BLUE;
	else if(Red.m_Progress != Blue.m_Progress) Winner=Red.m_Progress > Blue.m_Progress ? TEAM_RED : TEAM_BLUE;
	char aBuf[128]; str_format(aBuf,sizeof(aBuf), Winner < 0 ? "CK map result: draw." : (Winner == TEAM_RED ? "CK map result: Red wins." : "CK map result: Blue wins.")); Announce(aBuf);
}

void CGameControllerCK::SendObjectiveStatus(int ClientID)
{
	char aAttack[96] = "", aRetake[96] = "";
	for(int i = 0; i < MAX_POINTS; ++i)
	{
		if(CanAttackPoint(i))
		{
			if(aAttack[0]) str_append(aAttack, ",", sizeof(aAttack));
			str_append(aAttack, m_aNodes[i].m_aLabel, sizeof(aAttack));
		}
		if(CanRetakePoint(i))
		{
			if(aRetake[0]) str_append(aRetake, ",", sizeof(aRetake));
			str_append(aRetake, m_aNodes[i].m_aLabel, sizeof(aRetake));
		}
	}
	if(!aAttack[0]) str_copy(aAttack, "-", sizeof(aAttack));
	if(!aRetake[0]) str_copy(aRetake, "-", sizeof(aRetake));
	char aBuf[512]; int Left = g_Config.m_SvTimelimit ? max(0, g_Config.m_SvTimelimit*60-(Server()->Tick()-m_RoundStartTick)/Server()->TickSpeed()) : -1;
	int CaptureSeconds = round_to_int(CaptureDuration());
	if(Left >= 0)
		str_format(aBuf,sizeof(aBuf), "CK round %d: %s attack, %d:%02d left, capture %ds, attack [%s], retake [%s]%s", m_AttackingTeam == TEAM_RED ? 1 : 2, m_AttackingTeam == TEAM_RED ? "Red" : "Blue", Left/60, Left%60, CaptureSeconds, aAttack, aRetake, m_FinalStage ? ", final flag stage" : "");
	else
		str_format(aBuf,sizeof(aBuf), "CK round %d: %s attack, no time limit, capture %ds, attack [%s], retake [%s]%s", m_AttackingTeam == TEAM_RED ? 1 : 2, m_AttackingTeam == TEAM_RED ? "Red" : "Blue", CaptureSeconds, aAttack, aRetake, m_FinalStage ? ", final flag stage" : "");
	GameServer()->SendChatTarget(ClientID,aBuf);
}

void CGameControllerCK::DoWincheck()
{
	if(!m_RoundFinished && m_MapValid && g_Config.m_SvTimelimit > 0 && Server()->Tick()-m_RoundStartTick >= g_Config.m_SvTimelimit*60*Server()->TickSpeed()) FinishRound("time expired", false);
}

void CGameControllerCK::Tick()
{
	IGameController::Tick();
	if(m_IntermissionTick >= 0)
	{
		if(Server()->Tick() <= m_IntermissionTick+ROUND_RESULT_DELAY*Server()->TickSpeed()) return;
		if(m_AttackingTeam == TEAM_RED) { m_AttackingTeam = TEAM_BLUE; StartRound(); Announce("Attack and defence exchanged: Blue attacks."); }
		else
		{
			AnnounceMapResult();
			// CK owns both legs of a map, so force the normal game-over path to
			// rotate after this result regardless of sv_rounds_per_map.
			m_RoundCount = g_Config.m_SvRoundsPerMap-1;
			m_ResetMatchOnNextRound = true;
			m_IntermissionTick = -1;
			EndRound();
		}
		return;
	}
	if(GameServer()->m_World.m_Paused || GameServer()->m_World.m_ResetRequested) return;
	ScanMap(); if(!m_MapValid) return;
	TickPoints(); SyncLegacyCheckpointProgress(); TickTeleports(); TickFlags();
}

void CGameControllerCK::SnapFlags(int SnappingClient)
{
	if(SnappingClient >= 0 && Server()->IsSixup(SnappingClient))
	{
		protocol7::CNetObj_GameDataFlag *p=(protocol7::CNetObj_GameDataFlag *)Server()->SnapNewItem(-protocol7::NETOBJTYPE_GAMEDATAFLAG,0,sizeof(*p)); if(!p) return;
		p->m_FlagDropTickRed=0; p->m_FlagDropTickBlue=0;
		p->m_FlagCarrierRed=m_apFlags[0] && m_apFlags[0]->m_AtStand ? protocol7::FLAG_ATSTAND : protocol7::FLAG_MISSING;
		p->m_FlagCarrierBlue=m_apFlags[1] && m_apFlags[1]->m_AtStand ? protocol7::FLAG_ATSTAND : protocol7::FLAG_MISSING;
		return;
	}
	CNetObj_GameData *p=(CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA,0,sizeof(*p)); if(!p) return;
	p->m_TeamscoreRed=p->m_TeamscoreBlue=0;
	p->m_FlagCarrierRed=m_apFlags[0] && m_apFlags[0]->m_AtStand ? FLAG_ATSTAND : FLAG_MISSING;
	p->m_FlagCarrierBlue=m_apFlags[1] && m_apFlags[1]->m_AtStand ? FLAG_ATSTAND : FLAG_MISSING;
}

void CGameControllerCK::Snap(int SnappingClient)
{
	IGameController::Snap(SnappingClient); SnapFlags(SnappingClient);
}
