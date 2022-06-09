#pragma once
//	SCMDRAFT 2 - COPYRIGHT 2001-202X STORMCOAST FORTRESS
//	BLA BLA BLA

#include "V3\\Tileset.h" // Included for the tileset specific types

//	Contains the raw ISOM matching data for a map, and utility functions
//	Does not do any actual matching or undo / redo stuff
class MapIsomData
{
public:
	typedef unsigned __int16	IsomValue;
	typedef unsigned __int16	IsomGroup; // Terrain isom group, either a specific border transition or raw terrain


	struct IsomRect
	{
		static const IsomValue	ISOM_FLAG_EDITED  = 0x0001;
		static const IsomValue	ISOM_FLAG_SKIPPED = 0x8000;
		static const size_t		DirectionIndices[];

		IsomValue			values[4];

		void				SetIsomValue(	__in const size_t dirIndex,
											__in const IsomValue value );
		void				SetRawIsomValue(	__in const size_t arrayIndex,
												__in const IsomValue value );
		unsigned __int16	GetRawIsomValue(	__in const size_t arrayIndex ) const;
		void				SetIsomValueChanged(	__in const size_t dirIndex );
		void				SetDirVisited(			__in const size_t dirIndex );
		void				ClearDirVisited(		__in const size_t dirIndex );
		bool				GetDirVisited(			__in const size_t dirIndex ) const;

		bool				GetChanged( void ) const; // values[0]
		bool				GetEitherLRChanged( void ) const; // values[0] | values[2]
		bool				GetBothLRChanged( void ) const; // values[0] & values[2]
		void				ClearChanged( void );
	};
	C_ASSERT( sizeof( MapIsomData::IsomRect ) == 8 );

							MapIsomData( void );
							~MapIsomData( void );

private:
	size_t					width;
	size_t					height;
public:
	size_t					GetWidth( void ) const { return this->width; }
	size_t					GetHeight( void ) const { return this->height; }

	static size_t			TileXPosToIsomXPos( __in const TileCoordinate xPosition) { return xPosition / 2; }
	static size_t			TileYPosToIsomYPos( __in const TileCoordinate yPosition) { return yPosition; }

protected:
	std::unique_ptr<IsomRect[]>	data;

public:
	//	Create the actual data store for the isom matching data
	HRESULT					Create(	__in const size_t mapWidth,
									__in const size_t mapHeight );

	HRESULT					CopyFrom(	__inout MapIsomData *isomData,
										__in const __int32 xOffset,
										__in const __int32 yOffset );


	//	Set the entire table to a default value.
	HRESULT					InitializeToValue(	__in const unsigned __int16 value );

	HRESULT					Load(	__in const size_t length,
									__in const unsigned __int16 *srcData );
	HRESULT					Save(	__in const size_t length,
									__inout unsigned __int16 *destData ) const;


	//	This (re)initializes the tables required for terrain matching...
	HRESULT					SetTilesetType(	__in const SCEngine::TilesetIndex tilesetID );

	unsigned __int16		GetIsomVal( __in const SCEngine::TileGroupID tileGroupID );
	size_t					GetNumIsomValues( void ) const { return this->tileToIsomTableLength; }

protected:
	HRESULT					GenerateMatchPathTable(	__in const DWORD *tileConnectionTable,
													__in const size_t maxIsomValue,
													__out std::unique_ptr<IsomGroup[]> *matchPathCache );

public:
	const DWORD				*isomDataTbl;
	size_t					isomDataTableLength;

protected:
	const DWORD				*tileToIsomTbl;
	size_t					tileToIsomTableLength;
public:
	//	tileToIsomTableLength x tileToIsomTableLength table which contains connections between tile types.
	std::unique_ptr<IsomGroup[]>	matchPathCache;

public:
	IsomRect*				GetIsomRect(	__in const size_t xPosition,
											__in const size_t yPosition );
	unsigned __int16		GetIsomValue(	__in const size_t xPosition,
											__in const size_t yPosition );
	bool					GetIsomValueChanged(	__in const size_t xPosition,
													__in const size_t yPosition );

	void					SetIsomValue(	__in const size_t xPosition,
											__in const size_t yPosition,
											__in const size_t dirIndex,
											__in const unsigned __int16 value );
	void					SetIsomValueChanged(	__in const size_t xPosition,
													__in const size_t yPosition,
													__in const size_t dirIndex );
};

