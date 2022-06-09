#ifndef SI__CIsoMap
#define SI__CIsoMap

#include <list>
#include "V3\\Map\\MapIsomData.h"
#include "CSCMDundo.h"

class CScmdraftUndo;

class IsomUndoNode
	:	public UndoNodeBase
{
public:
							IsomUndoNode( void ) : UndoNodeBase( UNDO_ISOMCHANGE ) {}

	TileCoordinate			xPos;
	TileCoordinate			yPos;

	struct Settings
	{
		MapIsomData::IsomRect	isom;
	};

	Settings				oldSettings;
	Settings				newSettings;
};


#define ISOM_LEFT	0
#define ISOM_TOP	1
#define ISOM_RIGHT	2
#define ISOM_BOTTOM	3


static const long	diamondNeighborOffsets[]	= { -1, -1,
													+1, -1,
													+1, +1,
													-1, +1 };

//	Take a diamond position, and adjust it to
//	find one of the four tile coordinates it covers.
static const long	diamondXYtoTileXY[]	= { -1, -1,
											 0, -1,
											 0,  0,
											-1,  0 };


struct SearchNode
{
	MapIsomData::IsomValue	neighborIsomVal[4];
	DWORD					neighborUnkVal[4];
	BOOL					neighborUpdated[4];
	DWORD					MatchCnt;
	DWORD					IsomVal;
	MapIsomData::IsomGroup	maxGroupVal;
};

struct MatchNode 
{
	POINT					position;
};


class MapTerrain;
class TerrainLayer;

class CIsoMap
{
protected:
	MapIsomData				*isomMatchingData;
	MapTerrain				*mapTerrain;
public:
							CIsoMap( void );
							~CIsoMap( void );

	HRESULT					Initialize(	__in MapIsomData *isomMatchingData,
										__in MapTerrain *mapTerrain );

	HRESULT					FinalizeResize(	__in const __int32 xOffset,
											__in const __int32 yOffset,
											__in const TileCoordinate oldMapWidth,
											__in const TileCoordinate oldMapHeight,
											__in const bool fixBorders );

	HRESULT					CopyFrom(	__inout MapIsomData *isomData,
										__in const __int32 xOffset,
										__in const __int32 yOffset,
										__in const DWORD undoID,
										__in CScmdraftUndo *undoList );

protected:
	DWORD					MakeHash(	__in const MapIsomData::IsomRect *isomRect );

	DWORD					GetTileHash(WORD X, WORD Y);


	std::unique_ptr<IsomUndoNode*[]>	undoNodeTable;

	TileRect				changedArea;
public:
	HRESULT					ResetChangedArea( void );

	HRESULT					PlaceTerrain(	__in const TileCoordinate X,
											__in const TileCoordinate Y,
											__in SCEngine::TileGroupID tileGroupID,
											__in const size_t brushExtent,
											__in const DWORD undoID,
											__in CScmdraftUndo *undoList );

	HRESULT					FinalizeTerrain(	__in TerrainLayer &terrainLayerEditor );
private:
	HRESULT					InternalPlaceIsom(	__in const TileCoordinate X,
												__in const TileCoordinate Y,
												__in const size_t brushExtent,
												__in DWORD isomVal,
												__in const DWORD undoID,
												__in CScmdraftUndo *undoList );

	bool					GetDiamondNeedsUpdate(	__in const TileCoordinate diamondX,
													__in const TileCoordinate diamondY );

	HRESULT					EnqueueTileUpdate(	__in const TileCoordinate diamondX,
												__in const TileCoordinate diamondY );

	// The changed area is in isom. coordinates
	HRESULT					InternalFinalizeTerrain(	__in const TileRect &changedArea,
														__in TerrainLayer &terrainLayerEditor );


private:
	HRESULT					PrepareUndoNode(	__in const TileCoordinate tileX,
												__in const TileCoordinate tileY,
												__in const DWORD undoID,
												__in CScmdraftUndo *undoList );

	HRESULT					SetDiamondIsom(		__in const TileCoordinate diamondX,
												__in const TileCoordinate diamondY,
												__in const MapIsomData::IsomValue isomVal,
												__in const DWORD undoID,
												__in CScmdraftUndo *undoList );

	HRESULT					SetTileIsom(		__in const TileCoordinate tileX,
												__in const TileCoordinate tileY,
												__in const size_t dir,
												__in const MapIsomData::IsomValue isomVal,
												__in const DWORD undoID,
												__in CScmdraftUndo *undoList );


	HRESULT					PrepareSearchNode(	__in const TileCoordinate diamondX,
												__in const TileCoordinate diamondY,
												__out SearchNode *matchData );
	
	bool					IsInBounds(	__in const TileCoordinate diamondX,
										__in const TileCoordinate diamondY );

	HRESULT					SearchForMatch(		__in const TileCoordinate diamondX,
												__in const TileCoordinate diamondY,
												__in const DWORD undoID,
												__in CScmdraftUndo *undoList );

	//	See if the isom value matches more sides than the current best match
	HRESULT					TestIsomValue(	__in const MapIsomData::IsomValue isomVal,
											__inout SearchNode *matchData );

	void					PlaceFinalTerrain(	__in const TileCoordinate X,
												__in const TileCoordinate Y,
												__in TerrainLayer &terrainLayerEditor );

	DWORD			LastUndoID;

	std::list<MatchNode>	isomStack;

};


#endif;

