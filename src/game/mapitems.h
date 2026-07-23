/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_MAPITEMS_H
#define GAME_MAPITEMS_H

// layer types
enum
{
	LAYERTYPE_INVALID=0,
	LAYERTYPE_GAME, // not used
	LAYERTYPE_TILES,
	LAYERTYPE_QUADS,

	MAPITEMTYPE_VERSION=0,
	MAPITEMTYPE_INFO,
	MAPITEMTYPE_IMAGE,
	MAPITEMTYPE_ENVELOPE,
	MAPITEMTYPE_GROUP,
	MAPITEMTYPE_LAYER,
	MAPITEMTYPE_ENVPOINTS,


	CURVETYPE_STEP=0,
	CURVETYPE_LINEAR,
	CURVETYPE_SLOW,
	CURVETYPE_FAST,
	CURVETYPE_SMOOTH,
	NUM_CURVETYPES,

	// game layer tiles
	ENTITY_NULL=0,
	ENTITY_SPAWN,
	ENTITY_SPAWN_RED,
	ENTITY_SPAWN_BLUE,
	ENTITY_FLAGSTAND_RED,
	ENTITY_FLAGSTAND_BLUE,
	ENTITY_ARMOR_1,
	ENTITY_HEALTH_1,
	ENTITY_WEAPON_SHOTGUN,
	ENTITY_WEAPON_GRENADE,
	ENTITY_POWERUP_NINJA,
	ENTITY_WEAPON_RIFLE,
	NUM_ENTITIES,

	// Battlefield entity indices passed to OnEntity (tile index - ENTITY_OFFSET).
	// Several reuse the vanilla pickup slots above.
	ENTITY_BF_HEALTH_STATION=6,
	ENTITY_BF_AMMO_STATION,
	ENTITY_BF_SMOKE_LAUNCHER,
	ENTITY_BF_WATER_ENABLE,
	ENTITY_BF_CHECK_BASE,
	ENTITY_BF_CHECK_FIRST,
	ENTITY_BF_CHECK_LAST=13,

	ENTITY_BF_VEHICLE_TEAM_STRIDE=16,
	ENTITY_BF_DOOR_START_FIRST,
	ENTITY_BF_DOOR_START_LAST=22,
	ENTITY_BF_HELI,
	ENTITY_BF_JET,
	ENTITY_BF_PANZER,
	ENTITY_BF_CAR,
	ENTITY_BF_CP_LINE_RED_FIRST,
	ENTITY_BF_CP_LINE_RED_LAST=29,
	ENTITY_BF_UBOAT,
	ENTITY_BF_SHIP,
	ENTITY_BF_MINI,
	ENTITY_BF_DOOR_END_FIRST=33,
	ENTITY_BF_DOOR_END_LAST=38,
	ENTITY_BF_CP_LINE_BLUE_FIRST=43,
	ENTITY_BF_CP_LINE_BLUE_LAST=45,
	ENTITY_BF_SWITCH_FIRST=49,
	ENTITY_BF_SWITCH_LAST=54,
	ENTITY_BF_SWITCH_BASE=48,
	ENTITY_BF_CP_DEST_FIRST=59,
	ENTITY_BF_CP_DEST_LAST=61,

	TILE_AIR=0,
	TILE_SOLID,
	TILE_DEATH,
	TILE_NOHOOK,
	TILE_WATER,

	// Battlefield game-layer tiles (absolute m_Index values).
	TILE_BF_HEALTH_STATION=6, // below ENTITY_OFFSET; OnEntity never sees it
	TILE_BF_ANTITANK=160,
	TILE_BF_CHECKPOINT1=170,
	TILE_BF_CHECKPOINT2=171,
	TILE_BF_CHECKPOINT3=172,

	TILE_BF_ANTIAIR=176,
	TILE_BF_CLASS_SOLDIER,
	TILE_BF_CLASS_ENGINEER,
	TILE_BF_CLASS_MEDIC,
	TILE_BF_CLASS_SNIPER,
	TILE_BF_REQUIRE_CLASS,
	TILE_BF_NOGO,
	TILE_BF_BASE_BLUE,
	TILE_BF_BASE_RED,
	TILE_BF_NOFLAG, // shoreline / no-flag zone
	TILE_BF_CHECK1,
	TILE_BF_CHECK2,
	TILE_BF_CHECK3,
	TILE_BF_WATER_HARPOON,
	TILE_BF_WATER_ARROWS,
	TILE_BF_WATER_STAR,

	TILEFLAG_VFLIP=1,
	TILEFLAG_HFLIP=2,
	TILEFLAG_OPAQUE=4,
	TILEFLAG_ROTATE=8,

	LAYERFLAG_DETAIL=1,
	TILESLAYERFLAG_GAME=1,

	ENTITY_OFFSET=255-16*4,
};

struct CPoint
{
	int x, y; // 22.10 fixed point
};

struct CColor
{
	int r, g, b, a;
};

struct CQuad
{
	CPoint m_aPoints[5];
	CColor m_aColors[4];
	CPoint m_aTexcoords[4];

	int m_PosEnv;
	int m_PosEnvOffset;

	int m_ColorEnv;
	int m_ColorEnvOffset;
};

class CTile
{
public:
	unsigned char m_Index;
	unsigned char m_Flags;
	unsigned char m_Skip;
	unsigned char m_Reserved;
};

struct CMapItemImage
{
	int m_Version;
	int m_Width;
	int m_Height;
	int m_External;
	int m_ImageName;
	int m_ImageData;
} ;

struct CMapItemGroup_v1
{
	int m_Version;
	int m_OffsetX;
	int m_OffsetY;
	int m_ParallaxX;
	int m_ParallaxY;

	int m_StartLayer;
	int m_NumLayers;
} ;


struct CMapItemGroup : public CMapItemGroup_v1
{
	enum { CURRENT_VERSION=3 };

	int m_UseClipping;
	int m_ClipX;
	int m_ClipY;
	int m_ClipW;
	int m_ClipH;

	int m_aName[3];
} ;

struct CMapItemLayer
{
	int m_Version;
	int m_Type;
	int m_Flags;
} ;

struct CMapItemLayerTilemap
{
	CMapItemLayer m_Layer;
	int m_Version;

	int m_Width;
	int m_Height;
	int m_Flags;

	CColor m_Color;
	int m_ColorEnv;
	int m_ColorEnvOffset;

	int m_Image;
	int m_Data;

	int m_aName[3];
} ;

struct CMapItemLayerQuads
{
	CMapItemLayer m_Layer;
	int m_Version;

	int m_NumQuads;
	int m_Data;
	int m_Image;

	int m_aName[3];
} ;

struct CMapItemVersion
{
	int m_Version;
} ;

struct CEnvPoint
{
	int m_Time; // in ms
	int m_Curvetype;
	int m_aValues[4]; // 1-4 depending on envelope (22.10 fixed point)

	bool operator<(const CEnvPoint &Other) { return m_Time < Other.m_Time; }
} ;

struct CMapItemEnvelope
{
	int m_Version;
	int m_Channels;
	int m_StartPoint;
	int m_NumPoints;
	int m_aName[8];
} ;

#endif
