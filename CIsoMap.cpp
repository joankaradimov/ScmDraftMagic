#include "SCMDGlobal.h"

#include "CIsoMap.h"

#include "CSCMDundo.h"
#include "V3\\Map\\StarcraftMap.h"
#include "V3\\LayerEditors\\TerrainEditor.h"
#include "CTileset.h"


CIsoMap::CIsoMap( void )
{
	this->isomMatchingData	= nullptr;

	this->LastUndoID	= 0xFFFFFFFF;

	this->mapTerrain	= nullptr;
}

CIsoMap::~CIsoMap(void)
{
	this->undoNodeTable = nullptr;
}


HRESULT CIsoMap::Initialize(	__in MapIsomData *isomMatchingData,
								__in MapTerrain *mapTerrain )
{
	HRESULT hr;
	VERIFYARG( isomMatchingData );
	VERIFYARG( mapTerrain );

	this->isomMatchingData	= isomMatchingData;
	this->mapTerrain		= mapTerrain;

	hr = ALLOCATE_UNIQUEPTR_ARRAY( undoNodeTable, IsomUndoNode*, this->isomMatchingData->GetWidth() * this->isomMatchingData->GetHeight() );
	RETURNHRSILENT_IF_ERROR( hr );
	::memset( this->undoNodeTable.get(), 0, sizeof(IsomUndoNode*) * this->isomMatchingData->GetWidth() * this->isomMatchingData->GetHeight() );

	hr = this->ResetChangedArea();
	RETURNHRSILENT_IF_ERROR( hr );

	return S_OK;
}



struct EdgeNode
{
	TileCoordinate	xPos;
	TileCoordinate	yPos;
	size_t			matchDistance;
};


HRESULT CIsoMap::FinalizeResize(	__in const __int32 xOffsetTiles,
									__in const __int32 yOffsetTiles,
									__in const TileCoordinate oldMapWidth,
									__in const TileCoordinate oldMapHeight,
									__in const bool fixBorders )
{
	HRESULT hr;
	VERIFYMEMBER( this->isomMatchingData );
	VERIFYMEMBER( this->mapTerrain );

	const __int32 xOffset = xOffsetTiles / 2;
	const __int32 yOffset = yOffsetTiles;

	size_t oldWidth  = MapIsomData::TileXPosToIsomXPos( oldMapWidth )  + 1;
	size_t oldHeight = MapIsomData::TileYPosToIsomYPos( oldMapHeight ) + 1;

	TileRect	sourceRc;
	hr = TileRect::CreateOffsettedSourceRect( oldWidth, oldHeight, this->isomMatchingData->GetWidth(), this->isomMatchingData->GetHeight(), xOffset, yOffset, &sourceRc );
	RETURNHRSILENT_IF_ERROR( hr );

	TileRect innerArea;
	innerArea.left   = sourceRc.left   + xOffset;
	innerArea.top    = sourceRc.top    + yOffset;
	innerArea.right  = sourceRc.right  + xOffset - 1; // -1 since the isom map extends past the tile map by 1
	innerArea.bottom = sourceRc.bottom + yOffset - 1; // -1 since the isom map extends past the tile map by 1

	//	Mark the inner area as already updated
	//	We also fix the diamonds on the boundary

	//	In order to make the stack of points which need to be updated
	//	contain a consistent order, we will spiral out from the center of the offsetted valid data.
	//	we probably should replace this with a spiral of only the outside row later on
	std::vector<EdgeNode> edgeNodes;

	for (size_t y=innerArea.top; y<innerArea.bottom + 1;++y)
	{
		for (size_t x=innerArea.left + ((innerArea.left + y) % 2);x<innerArea.right + 1;x += 2)
		{
			if ((y + x) % 2 != 0)
				continue;

			//	Figure out if the node is fully within the inner area
			bool fullyInside = true;
			bool fullyOutside = true;
			unsigned __int16 isomValue = 0;
			for (size_t i=0;i<4;i++)
			{
				TileCoordinate diamondX = x + diamondXYtoTileXY[i * 2 + 0];
				TileCoordinate diamondY = y + diamondXYtoTileXY[i * 2 + 1];
				if (! IsInBounds(diamondX, diamondY))
					continue;

				if (diamondX >= innerArea.left && diamondX < innerArea.right &&
					diamondY >= innerArea.top && diamondY < innerArea.bottom )
				{
					unsigned __int16 newIsomValue  = this->isomMatchingData->GetIsomRect( diamondX, diamondY )->GetRawIsomValue( MapIsomData::IsomRect::DirectionIndices[i * 2 + 0] ) >> 4;
//					unsigned __int16 newIsomValueB = this->isomMatchingData->GetIsomRect( diamondX, diamondY )->GetRawIsomValue( MapIsomData::IsomRect::DirectionIndices[i * 2 + 1] ) >> 4;
//					if (isomValue != 0 && (newIsomValue != isomValue || newIsomValueB != isomValue))
//						int brkit = 1;

					isomValue = newIsomValue;
					fullyOutside = false;
					continue;
				}

				fullyInside = false;
			}

			//	Skip diamonds that are completely outside
			if (fullyOutside)
				continue;

			//	If the diamond was not fully inside the map, then 
			//	expand its contents, and add surrounding points to the isom stack
			if (! fullyInside)
			{
				for (size_t i=0;i<4;i++)
				{
					TileCoordinate tileX = x + diamondXYtoTileXY[i * 2 + 0];
					TileCoordinate tileY = y + diamondXYtoTileXY[i * 2 + 1];
					if (! IsInBounds(tileX, tileY))
						continue;

					if (tileX >= innerArea.left && tileX < innerArea.right &&
						tileY >= innerArea.top && tileY < innerArea.bottom )
					{
						continue;
					}

					this->SetTileIsom( tileX, tileY, i, isomValue, 0, nullptr );
				}

				if (fixBorders)
				{
					for (size_t i=0;i<4;i++)
					{
						TileCoordinate diamondX = x + diamondNeighborOffsets[i * 2 + 0];
						TileCoordinate diamondY = y + diamondNeighborOffsets[i * 2 + 1];
						if (! IsInBounds(diamondX, diamondY))
							continue;

						if (diamondX >= innerArea.left && diamondX < innerArea.right &&
							diamondY >= innerArea.top && diamondY < innerArea.bottom )
						{
							continue;
						}

						EdgeNode newNode;
						newNode.xPos = diamondX;
						newNode.yPos = diamondY;
						newNode.matchDistance = 0;

						//	Determine number of isom types to traverse to get to the target terrain type
						MapIsomData::IsomValue targetIsomValue = isomValue; // XXX: hardcoded
						MapIsomData::IsomGroup targetGroupValue = this->isomMatchingData->isomDataTbl[13 * targetIsomValue + 0];

						MapIsomData::IsomValue curIsomVal   = this->isomMatchingData->GetIsomValue( diamondX, diamondY );
						if (curIsomVal * 13UL < this->isomMatchingData->isomDataTableLength)
						{
							MapIsomData::IsomGroup curGroupType = this->isomMatchingData->isomDataTbl[13 * curIsomVal + 0];

							++newNode.matchDistance;
							while (this->isomMatchingData->matchPathCache[this->isomMatchingData->GetNumIsomValues() * curGroupType + targetGroupValue] != targetGroupValue)
							{
								curGroupType = this->isomMatchingData->matchPathCache[this->isomMatchingData->GetNumIsomValues() * curGroupType + targetGroupValue];
								++newNode.matchDistance;
							}
						}

						edgeNodes.push_back( newNode );
					}
				}
			}

			for (size_t i=0;i<4;i++)
			{
				TileCoordinate diamondX = x + diamondXYtoTileXY[i * 2 + 0];
				TileCoordinate diamondY = y + diamondXYtoTileXY[i * 2 + 1];
				if (! IsInBounds(diamondX, diamondY))
					continue;

				this->isomMatchingData->GetIsomRect( diamondX, diamondY )->SetIsomValueChanged( i );
			}
		}
	}

///	this->SetIsom( 25, 31, 0x0001, 0, nullptr );
///	this->SetIsom( 15, 57, 0x0001, 0, nullptr );
///
///	this->SetIsom( 37, 97, 0x0001, 0, nullptr );
///	this->SetIsom( 49, 71, 0x0001, 0, nullptr );


	//	Order the edge nodes by complexity of the transition and then by distance from the top right
	std::sort(	edgeNodes.begin(), edgeNodes.end(),
				[](__in const EdgeNode &a, __in const EdgeNode &b) -> bool
				{
//					if (a.matchDistance != b.matchDistance)
//						return a.matchDistance > b.matchDistance;

					//	Order by distance from the top left corner
					unsigned long distanceA = a.xPos + a.yPos;
					unsigned long distanceB = b.xPos + b.yPos;
					if (distanceA != distanceB)
						return distanceA < distanceB;

					//	And then by distance from the diagonal
					distanceA = (std::max)(a.xPos, a.yPos) - (std::min)(a.xPos, a.yPos);
					distanceB = (std::max)(b.xPos, b.yPos) - (std::min)(b.xPos, b.yPos);
					if (distanceA != distanceB)
						return distanceA < distanceB;
					return a.xPos < b.xPos;
				} );
///	EdgeNode temp;
///	temp.xPos = 25; temp.yPos = 31; edgeNodes.insert( edgeNodes.begin(), temp );
///	temp.xPos = 15; temp.yPos = 57; edgeNodes.insert( edgeNodes.begin(), temp );
///	temp.xPos = 37; temp.yPos = 97; edgeNodes.insert( edgeNodes.begin(), temp );
///	temp.xPos = 49; temp.yPos = 71; edgeNodes.insert( edgeNodes.begin(), temp );

	//	Update the isom data for each edge node one at a time
	for (size_t k=0;k<edgeNodes.size();++k)
	{
		hr = this->EnqueueTileUpdate( edgeNodes[k].xPos, edgeNodes[k].yPos );
		RETURNHRSILENT_IF_ERROR( hr );
	}
	{
		//	And match the terrain
		size_t numSearches = 0;

		while (! this->isomStack.empty())
		{
			MatchNode curNode = this->isomStack.front(); this->isomStack.pop_front();

			++numSearches;
			if (! this->GetDiamondNeedsUpdate( curNode.position.x, curNode.position.y ) )
				continue;

			this->SearchForMatch( curNode.position.x, curNode.position.y, 0, nullptr);
		}

		//	Reset the changed  and visited area
		for (TileCoordinate yPosition=changedArea.top;yPosition<=changedArea.bottom;++yPosition)
		{
			for (TileCoordinate xPosition=changedArea.left;xPosition<=changedArea.right;xPosition++)
			{
				this->isomMatchingData->GetIsomRect( xPosition, yPosition )->ClearChanged();
			}
		}

		for (size_t y=innerArea.top; y<innerArea.bottom + 1;++y)
		{
			for (size_t x=innerArea.left + ((innerArea.left + y) % 2);x<innerArea.right + 1;x += 2)
			{
				if ((y + x) % 2 != 0)
					continue;

				//	Figure out if the node is fully within the inner area
				bool fullyOutside = true;
				for (size_t i=0;i<4;i++)
				{
					TileCoordinate diamondX = x + diamondXYtoTileXY[i * 2 + 0];
					TileCoordinate diamondY = y + diamondXYtoTileXY[i * 2 + 1];
					if (! IsInBounds(diamondX, diamondY))
						continue;

					if (diamondX >= innerArea.left && diamondX < innerArea.right &&
						diamondY >= innerArea.top && diamondY < innerArea.bottom )
					{
						fullyOutside = false;
						break;
					}
				}

				//	Skip diamonds that are completely outside
				if (fullyOutside)
					continue;

				for (size_t i=0;i<4;i++)
				{
					TileCoordinate diamondX = x + diamondXYtoTileXY[i * 2 + 0];
					TileCoordinate diamondY = y + diamondXYtoTileXY[i * 2 + 1];
					if (! IsInBounds(diamondX, diamondY))
						continue;

					this->isomMatchingData->GetIsomRect( diamondX, diamondY )->SetIsomValueChanged( i );
				}
			}
		}
	}

	this->isomStack.clear();



	//	Mark the inner area as unchanged, and all of the new tiles as changed, to trigger a full update
	//	Mark the inner area as already updated
	//	Note: we are excluding the right and bottom row, since these are outside of the map bounds.
	this->changedArea.left   = this->changedArea.top = 0;
	this->changedArea.right  = this->isomMatchingData->GetWidth() - 1;
	this->changedArea.bottom = this->isomMatchingData->GetHeight() - 1;
	for (size_t y=innerArea.top;y<innerArea.bottom;++y)
	{
		for (size_t x=innerArea.left;x<innerArea.right;++x)
		{
			MapIsomData::IsomRect *targetRect = this->isomMatchingData->GetIsomRect( x, y );
			targetRect->ClearChanged();
		}
	}

	for (size_t y=0;y<this->isomMatchingData->GetHeight();++y)
	{
		for (size_t x=0 + (y % 2);x<this->isomMatchingData->GetWidth();x += 2)
		{
			if ((y + x) % 2 != 0)
				continue;

			//	Figure out if the node is fully within the inner area
			bool fullyInside = true;
			for (size_t i=0;i<4;i++)
			{
				TileCoordinate diamondX = x + diamondXYtoTileXY[i * 2 + 0];
				TileCoordinate diamondY = y + diamondXYtoTileXY[i * 2 + 1];
				if (! IsInBounds(diamondX, diamondY))
					continue;

				if (diamondX >= innerArea.left && diamondX < innerArea.right &&
					diamondY >= innerArea.top && diamondY < innerArea.bottom )
				{
					continue;
				}

				fullyInside = false;
				break;
			}

			if (fullyInside)
				continue;

			//	If the diamond was not fully inside the map, then 
			//	expand its contents, and add surrounding points to the isom stack
			for (size_t i=0;i<4;i++)
			{
				TileCoordinate diamondX = x + diamondXYtoTileXY[i * 2 + 0];
				TileCoordinate diamondY = y + diamondXYtoTileXY[i * 2 + 1];
				if (! IsInBounds(diamondX, diamondY))
					continue;

				MapIsomData::IsomRect *targetRect = this->isomMatchingData->GetIsomRect( diamondX, diamondY );
				targetRect->SetIsomValueChanged( i );
			}
		}
	}

	return S_OK;
}

HRESULT CIsoMap::CopyFrom(	__inout MapIsomData *isomData,
							__in const __int32 xOffsetTiles,
							__in const __int32 yOffsetTiles,
							__in const DWORD undoID,
							__in CScmdraftUndo *undoList )
{
	HRESULT hr;
	VERIFYARG( isomData );
	VERIFYMEMBER( isomMatchingData );

	if (undoID && undoList)
	{
		const __int32 xOffset = xOffsetTiles / 2;
		const __int32 yOffset = yOffsetTiles;

		TileRect	sourceRc;
		hr = TileRect::CreateOffsettedSourceRect( isomData->GetWidth(), isomData->GetHeight(), this->isomMatchingData->GetWidth(), this->isomMatchingData->GetHeight(), xOffset, yOffset, &sourceRc );
		RETURNHRSILENT_IF_ERROR( hr );

		for (TileCoordinate y=0;y < this->isomMatchingData->GetHeight();++y)
		{
			for (TileCoordinate x=0;x < this->isomMatchingData->GetWidth();++x)
			{
				hr = this->PrepareUndoNode( x, y, undoID, undoList );
				RETURNHRSILENT_IF_ERROR( hr );
			}
		}

		hr = this->isomMatchingData->CopyFrom( isomData, xOffset, yOffset );
		RETURNHRSILENT_IF_ERROR( hr );

		//	Reset isom values that will be clipped after resizing to 0.
		for (TileCoordinate y = isomData->GetHeight();y<this->isomMatchingData->GetHeight();++y)
		{
			for (TileCoordinate x = 0;x<this->isomMatchingData->GetWidth();++x)
			{
				MapIsomData::IsomRect *targetRect = this->isomMatchingData->GetIsomRect( x, y );
				targetRect->SetRawIsomValue(0, 0x0000);
				targetRect->SetRawIsomValue(1, 0x0000);
				targetRect->SetRawIsomValue(2, 0x0000);
				targetRect->SetRawIsomValue(3, 0x0000);
			}
		}

		if (isomData->GetWidth() < this->isomMatchingData->GetWidth())
		{
			for (TileCoordinate y = 0;y<this->isomMatchingData->GetHeight();++y)
			{
				for (TileCoordinate x = isomData->GetWidth();x<this->isomMatchingData->GetWidth();++x)
				{
					MapIsomData::IsomRect *targetRect = this->isomMatchingData->GetIsomRect( x, y );
					targetRect->SetRawIsomValue(0, 0x0000);
					targetRect->SetRawIsomValue(1, 0x0000);
					targetRect->SetRawIsomValue(2, 0x0000);
					targetRect->SetRawIsomValue(3, 0x0000);
				}
			}
		}

		for (TileCoordinate y=0;y < this->isomMatchingData->GetHeight();++y)
		{
			for (TileCoordinate x=0;x < this->isomMatchingData->GetWidth();++x)
			{
				IsomUndoNode *undoNode = this->undoNodeTable[x + y * this->isomMatchingData->GetWidth()];
				MapIsomData::IsomRect *targetRect = this->isomMatchingData->GetIsomRect( x, y );

				undoNode->newSettings.isom.SetRawIsomValue(0, targetRect->GetRawIsomValue(0) );
				undoNode->newSettings.isom.SetRawIsomValue(1, targetRect->GetRawIsomValue(1) );
				undoNode->newSettings.isom.SetRawIsomValue(2, targetRect->GetRawIsomValue(2) );
				undoNode->newSettings.isom.SetRawIsomValue(3, targetRect->GetRawIsomValue(3) );
			}
		}
	}
	else
	{
		hr = this->isomMatchingData->CopyFrom( isomData, xOffsetTiles, yOffsetTiles );
		RETURNHRSILENT_IF_ERROR( hr );
	}

	return S_OK;
}






HRESULT CIsoMap::PrepareUndoNode(	__in const TileCoordinate tileX,
									__in const TileCoordinate tileY,
									__in const DWORD undoID,
									__in CScmdraftUndo *undoList )
{
	VERIFYARG( undoList );

	if (undoID != this->LastUndoID)
	{
		this->LastUndoID = undoID;
		::memset( this->undoNodeTable.get(), 0, sizeof(IsomUndoNode*) * this->isomMatchingData->GetWidth() * this->isomMatchingData->GetHeight() );
	}

	if (this->undoNodeTable[tileX + tileY * this->isomMatchingData->GetWidth()] )
		return S_OK;

	IsomUndoNode *undoNode = new (std::nothrow) IsomUndoNode;
	if (! undoNode)
		return E_OUTOFMEMORY;

	undoNode->xPos = tileX;
	undoNode->yPos = tileY;

	MapIsomData::IsomRect *targetRect = this->isomMatchingData->GetIsomRect( tileX, tileY );
	undoNode->oldSettings.isom.SetRawIsomValue(0, targetRect->GetRawIsomValue(0) );
	undoNode->oldSettings.isom.SetRawIsomValue(1, targetRect->GetRawIsomValue(1) );
	undoNode->oldSettings.isom.SetRawIsomValue(2, targetRect->GetRawIsomValue(2) );
	undoNode->oldSettings.isom.SetRawIsomValue(3, targetRect->GetRawIsomValue(3) );

	this->undoNodeTable[tileX + tileY * this->isomMatchingData->GetWidth()] = undoNode;
	undoList->AddUndoNode( undoID, std::unique_ptr<IsomUndoNode>( undoNode ) );

	return S_OK;
}


HRESULT CIsoMap::SetDiamondIsom(	__in const TileCoordinate diamondX,
									__in const TileCoordinate diamondY,
									__in const MapIsomData::IsomValue isomVal,
									__in const DWORD undoID,
									__in CScmdraftUndo *undoList )
{
	HRESULT hr;

	for (size_t curDir=0;curDir<4;++curDir)
	{
		TileCoordinate tileX = diamondX + diamondXYtoTileXY[curDir * 2 + 0];
		TileCoordinate tileY = diamondY + diamondXYtoTileXY[curDir * 2 + 1];
		if (! IsInBounds( tileX, tileY ))
			continue;

		hr = this->SetTileIsom(tileX, tileY, curDir, isomVal, undoID, undoList);
		RETURNHRSILENT_IF_ERROR( hr );
	}

	return S_OK;
}


HRESULT CIsoMap::SetTileIsom(	__in const TileCoordinate tileX,
								__in const TileCoordinate tileY,
								__in const size_t dir,
								__in const MapIsomData::IsomValue isomVal,
								__in const DWORD undoID,
								__in CScmdraftUndo *undoList )
{
	HRESULT hr;
	UNREFERENCED_PARAMETER( hr );

	MapIsomData::IsomRect *targetRect = this->isomMatchingData->GetIsomRect( tileX, tileY );

	IsomUndoNode *undoNode = nullptr;
	if (undoList && this->undoNodeTable)
	{
		this->PrepareUndoNode( tileX, tileY, undoID, undoList );
		undoNode = this->undoNodeTable[tileX + tileY * this->isomMatchingData->GetWidth()];
	}

	targetRect->SetIsomValue( dir, isomVal );
	targetRect->SetIsomValueChanged( dir );
	targetRect->ClearDirVisited( dir );

	if (undoNode)
	{
		undoNode->newSettings.isom.SetRawIsomValue(0, targetRect->GetRawIsomValue(0) );
		undoNode->newSettings.isom.SetRawIsomValue(1, targetRect->GetRawIsomValue(1) );
		undoNode->newSettings.isom.SetRawIsomValue(2, targetRect->GetRawIsomValue(2) );
		undoNode->newSettings.isom.SetRawIsomValue(3, targetRect->GetRawIsomValue(3) );
	}

	this->changedArea.left   = (std::min)(this->changedArea.left,   tileX );
	this->changedArea.right  = (std::max)(this->changedArea.right,  tileX );
	this->changedArea.top    = (std::min)(this->changedArea.top,    tileY );
	this->changedArea.bottom = (std::max)(this->changedArea.bottom, tileY );

	return S_OK;
}

void CIsoMap::PlaceFinalTerrain(	__in const TileCoordinate X,
									__in const TileCoordinate Y,
									__in TerrainLayer &terrainLayerEditor )
{
	HRESULT hr;

	if (X + 1 >= this->isomMatchingData->GetWidth() || Y + 1 >= this->isomMatchingData->GetHeight())
		return;

	const SI_CTileset *tileset = this->mapTerrain->GetTileset();

	DWORD TileHash = GetTileHash(X, Y);
	const std::vector<CMegaGroupNode>	*potentialTileList = tileset->GetHashArray(TileHash);
	
	if (potentialTileList != NULL)
	{
		unsigned __int16 destTileGroup = (*potentialTileList)[0].groupIndex;

		//	Use the tile row above the current one to determine the exact type of tile group to use
		//	Random guess: this facilitates cliff stacking
		if (Y != 0)
		{
			const TerrainData::TileGroupInfo *prvRowTileGroupInfo = tileset->GetTileGroup( this->mapTerrain->GetBaseTileIndex(X * 2, Y - 1) );
			if (prvRowTileGroupInfo)
			{
				unsigned __int16 prvRowTileGroupMatching = prvRowTileGroupInfo->intraGroupMatching[3];
				for (size_t i=0;i<potentialTileList->size();++i)
				{
					if ((*potentialTileList)[i].tileGroupRef->intraGroupMatching[1] != prvRowTileGroupMatching)
						continue;

					destTileGroup = (*potentialTileList)[i].groupIndex;
					break;
				}
			}
		}

		//	Set the actual tile values
		unsigned __int16 destSubTile;
		tileset->GetRandomSubtile(destTileGroup, &destSubTile);
		destSubTile = destSubTile % 16;

		hr = terrainLayerEditor.SetBaseTileIndex( X * 2 + 0, Y, (destTileGroup + 0) * 16 + destSubTile );
		hr = terrainLayerEditor.SetBaseTileIndex( X * 2 + 1, Y, (destTileGroup + 1) * 16 + destSubTile );


		//	Find the top row of the set of linked tile group transitions
		TileCoordinate yPosition = Y;
		if (yPosition > 0)
		{
			//	Cache the previous row's value -> then we only need to grab one row per loop iteration
			const TerrainData::TileGroupInfo *prvRowTileGroupInfo = tileset->GetTileGroup( this->mapTerrain->GetBaseTileIndex(X * 2, yPosition) );
			while (yPosition > 0)
			{
				//	Swap the previous row to this iteration's current row
				const TerrainData::TileGroupInfo *curRowTileGroupInfo = prvRowTileGroupInfo;
				if (! curRowTileGroupInfo || curRowTileGroupInfo->intraGroupMatching[1] == 0)
					break;

				prvRowTileGroupInfo = tileset->GetTileGroup( this->mapTerrain->GetBaseTileIndex(X * 2, yPosition - 1) );
				if (! prvRowTileGroupInfo || curRowTileGroupInfo->intraGroupMatching[1] != prvRowTileGroupInfo->intraGroupMatching[3])
					break;

				--yPosition;
			}
		}

		//	Set subtile of the top row of the cliff stack
		hr = terrainLayerEditor.SetBaseTileIndex( X * 2 + 0, yPosition, SCEngine::GetTileGroupIndex( this->mapTerrain->GetBaseTileIndex( X * 2 + 0, yPosition ) ) * 16 + destSubTile );
		hr = terrainLayerEditor.SetBaseTileIndex( X * 2 + 1, yPosition, SCEngine::GetTileGroupIndex( this->mapTerrain->GetBaseTileIndex( X * 2 + 1, yPosition ) ) * 16 + destSubTile );

		//	And now set terrain + subtiles of the rest of the stack
		++yPosition;
		while (yPosition < this->mapTerrain->GetHeight())
		{
			const TerrainData::TileGroupInfo *curRowTileGroupInfo = tileset->GetTileGroup( this->mapTerrain->GetBaseTileIndex(X * 2, yPosition - 1) );
			if (! curRowTileGroupInfo)
				break;
			unsigned __int16 curRowTileGroupMatching = curRowTileGroupInfo->intraGroupMatching[3];
			const TerrainData::TileGroupInfo *nxtRowTileGroupInfo = tileset->GetTileGroup( this->mapTerrain->GetBaseTileIndex(X * 2, yPosition + 0) );
			if (! nxtRowTileGroupInfo)
				break;
			unsigned __int16 nxtRowTileGroupMatching = nxtRowTileGroupInfo->intraGroupMatching[1];
			if (curRowTileGroupMatching == 0 || nxtRowTileGroupMatching == 0)
				break;

			//	Remember these seperately, for failure case.
			//	Not sure this is really needed
			SCEngine::TileGroupIndex destTileGroupA = SCEngine::GetTileGroupIndex( this->mapTerrain->GetBaseTileIndex( X * 2 + 0, yPosition ) );
			SCEngine::TileGroupIndex destTileGroupB = SCEngine::GetTileGroupIndex( this->mapTerrain->GetBaseTileIndex( X * 2 + 1, yPosition ) );
			if (curRowTileGroupMatching != nxtRowTileGroupMatching)
			{
				TileHash = GetTileHash(X, yPosition );
				const std::vector<CMegaGroupNode>	*potentialTileList = tileset->GetHashArray(TileHash);

				if (potentialTileList != NULL)
				{
					for (size_t i=0;i<potentialTileList->size();++i)
					{
						if ((*potentialTileList)[i].tileGroupRef->intraGroupMatching[1] != curRowTileGroupMatching)
							continue;

						destTileGroupA = ((*potentialTileList)[i].groupIndex + 0);
						destTileGroupB = destTileGroupA + 1;
						break;
					}
				}
			}

			hr = terrainLayerEditor.SetBaseTileIndex( X * 2 + 0, yPosition, destTileGroupA * 16 + destSubTile );
			hr = terrainLayerEditor.SetBaseTileIndex( X * 2 + 1, yPosition, destTileGroupB * 16 + destSubTile );
			++yPosition;
		}
			
	}
	else
	{
		hr = terrainLayerEditor.SetBaseTileIndex( X * 2 + 0, Y, 0 );
		hr = terrainLayerEditor.SetBaseTileIndex( X * 2 + 1, Y, 0 );
	}
}


HRESULT CIsoMap::ResetChangedArea( void )
{
	this->changedArea.left   = this->isomMatchingData->GetWidth();
	this->changedArea.right  = 0;
	this->changedArea.top    = this->isomMatchingData->GetHeight();
	this->changedArea.bottom = 0;

	return S_OK;
}

HRESULT CIsoMap::PlaceTerrain(	__in const TileCoordinate diamondX,
								__in const TileCoordinate diamondY,
								__in SCEngine::TileGroupID tileGroupID,
								__in const size_t brushExtent,
								__in const DWORD undoID,
								__in CScmdraftUndo *undoList )
{
	HRESULT hr;
	if (! this->isomMatchingData)
		return false;

	unsigned __int16 newTerrainIsomVal = this->isomMatchingData->GetIsomVal( tileGroupID );
	if (newTerrainIsomVal == 0)
		return E_INVALIDARG;

	//	XXX: This shouldn't be required ...
	hr = this->ResetChangedArea();
	RETURNHRSILENT_IF_ERROR( hr );

	hr = this->InternalPlaceIsom( diamondX, diamondY, brushExtent, newTerrainIsomVal, undoID, undoList );
	RETURNHRSILENT_IF_ERROR( hr );

	return S_OK;
}

HRESULT CIsoMap::FinalizeTerrain(	__in TerrainLayer &terrainLayerEditor )
{
	HRESULT hr;

	hr = this->InternalFinalizeTerrain( this->changedArea, terrainLayerEditor );
	RETURNHRSILENT_IF_ERROR( hr );

	hr = this->ResetChangedArea();
	RETURNHRSILENT_IF_ERROR( hr );

	return S_OK;
}


HRESULT CIsoMap::InternalPlaceIsom(	__in const TileCoordinate tileX,
									__in const TileCoordinate tileY,
									__in const size_t brushExtent,
									__in DWORD isomVal,
									__in const DWORD undoID,
									__in CScmdraftUndo *undoList )

{
	HRESULT hr;
	VERIFYMEMBER( this->isomMatchingData );

	if ( (tileX + tileY) % 2 == 1)
		return E_INVALIDARG;

	if (isomVal * 13UL >= this->isomMatchingData->isomDataTableLength ||
		this->isomMatchingData->isomDataTbl[isomVal * 13 + 0] == 0x00)
	{
		return false;
	}

	int SizeStart	=	-static_cast<int>(brushExtent) / 2;
	int SizeEnd		=	SizeStart + brushExtent;
 	if (brushExtent % 2 == 0)
	{
		SizeStart++;
		SizeEnd++;
	}

	hr = this->ResetChangedArea();

 	for (int Xmod = SizeStart;Xmod < SizeEnd; Xmod++)
	{
		for (int Ymod = SizeStart;Ymod < SizeEnd; Ymod++)
		{
			TileCoordinate diamondX = tileX + Xmod - Ymod;
			TileCoordinate diamondY = tileY + Xmod + Ymod;
			if (! IsInBounds(diamondX, diamondY))
				continue;

			SetDiamondIsom(diamondX, diamondY, isomVal, undoID, undoList);

			//	Only enqueue tile updates for the outside edge
			if (Xmod != SizeStart && Ymod != SizeStart && Xmod != SizeEnd - 1 && Ymod != SizeEnd - 1)
				continue;

			for (size_t i=0;i<4;i++)
			{
				TileCoordinate neighborDiamondX = diamondX + diamondNeighborOffsets[i * 2 + 0];
				TileCoordinate neighborDiamondY = diamondY + diamondNeighborOffsets[i * 2 + 1];

				hr = this->EnqueueTileUpdate( neighborDiamondX, neighborDiamondY );
				RETURNHRSILENT_IF_ERROR( hr );
			}
		}
	}

//	size_t isomStepCount = isomStack.size();
//	size_t processedCount = 0;
//	SIErrorLogger			logger("Isom");
	while (! isomStack.empty())
	{
		const MatchNode curNode = isomStack.front(); isomStack.pop_front();

		if (this->GetDiamondNeedsUpdate( curNode.position.x, curNode.position.y ))
		{
			SearchForMatch(curNode.position.x, curNode.position.y, undoID, undoList);
//			logger.ReportWarning("Isom Node: [%4d, %4d]", curNode.position.x, curNode.position.y);
		}
//		else
//		{
//			logger.ReportInfo("Isom Node: [%4d, %4d]", curNode.position.x, curNode.position.y);
//		}
//
//		++processedCount;
//		if (processedCount == isomStepCount)
//		{
//			isomStepCount = isomStack.size();
//			processedCount = 0;
//			logger.ReportInfo("Isom pass completed\r\n");
//		}
	}

	return S_OK;
}

bool CIsoMap::GetDiamondNeedsUpdate(	__in const TileCoordinate diamondX,
										__in const TileCoordinate diamondY )
{
	if (! IsInBounds(diamondX, diamondY) )
		return false;

	//	Don't add nodes we know won't be needed
	if (this->isomMatchingData->GetIsomValueChanged( diamondX, diamondY ))
		return false;
	if (this->isomMatchingData->GetIsomValue( diamondX, diamondY ) == 0)
		return false;

	return true;
}


HRESULT CIsoMap::EnqueueTileUpdate(	__in const TileCoordinate diamondX,
									__in const TileCoordinate diamondY )
{
	if (! this->GetDiamondNeedsUpdate(diamondX, diamondY) )
		return S_FALSE;

	MatchNode newNode;
	newNode.position.x = diamondX;
	newNode.position.y = diamondY;
	this->isomStack.push_back( newNode );

	return S_OK;
}


HRESULT CIsoMap::InternalFinalizeTerrain(	__in const TileRect &changedArea,
											__in TerrainLayer &terrainLayerEditor )
{
	HRESULT hr;
	UNREFERENCED_PARAMETER( hr );

	for (TileCoordinate yPosition=changedArea.top;yPosition<=changedArea.bottom;++yPosition)
	{
		for (TileCoordinate xPosition=changedArea.left;xPosition<=changedArea.right;xPosition++)
		{
//			if ((xPosition + yPosition) % 2 != 0)
//				continue;
			MapIsomData::IsomRect *curRect = this->isomMatchingData->GetIsomRect( xPosition, yPosition );
			if (curRect->GetEitherLRChanged())
			{
				this->PlaceFinalTerrain( xPosition, yPosition, terrainLayerEditor );
			}

			curRect->ClearChanged();
		}
	}

	return S_OK;
}


bool CIsoMap::IsInBounds(	__in const TileCoordinate diamondX,
							__in const TileCoordinate diamondY )
{
	if (diamondX >= this->isomMatchingData->GetWidth())
		return false;
	if (diamondY >= this->isomMatchingData->GetHeight())
		return false;

	return true;
}

HRESULT CIsoMap::PrepareSearchNode(	__in const TileCoordinate diamondX,
									__in const TileCoordinate diamondY,
									__out SearchNode *matchData )
{
	VERIFYARG( matchData );
	matchData->IsomVal		=	0x00;
	matchData->MatchCnt		=	0x00;
	matchData->maxGroupVal	=	0x00;

	for (size_t curDir=0;curDir<4;++curDir)
	{
		matchData->neighborIsomVal[curDir]	=	0x00;
		matchData->neighborUnkVal[curDir]	=	0x00;
		matchData->neighborUpdated[curDir]	=	FALSE;
	}
	for (size_t curDir=0;curDir<4;++curDir)
	{
		TileCoordinate neighborX = diamondX + diamondNeighborOffsets[curDir * 2 + 0];
		TileCoordinate neighborY = diamondY + diamondNeighborOffsets[curDir * 2 + 1];
		if (! IsInBounds( neighborX, neighborY ) )
			continue;

		matchData->neighborIsomVal[curDir]	=	this->isomMatchingData->GetIsomValue( neighborX, neighborY );
		matchData->neighborUpdated[curDir]	=	this->isomMatchingData->GetIsomValueChanged( neighborX, neighborY ) ? TRUE : FALSE;

		//	Isom tile group row is the isom value * 13 term
		//	Dir 0: => index  9
		//	Dir 1: => index 12
		//	Dir 2: => index  3
		//	Dir 3: => index  6
		size_t inverseDir = (curDir + 2) & 0x03;
		matchData->neighborUnkVal[curDir]	=	this->isomMatchingData->isomDataTbl[matchData->neighborIsomVal[curDir] * 13 + (inverseDir + 1) * 3];

		if (! matchData->neighborUpdated[curDir])
			continue;

		//	Unknown commented check
//		if (MatchDataNode.TileVal != 0)

		//	Range check
		if (matchData->neighborIsomVal[curDir] * 13 >= this->isomMatchingData->isomDataTableLength)
			continue;

		matchData->maxGroupVal = (std::max)(matchData->maxGroupVal, static_cast<MapIsomData::IsomGroup>( this->isomMatchingData->isomDataTbl[13 * matchData->neighborIsomVal[curDir] + 0] ) );
	}

	return S_OK;
}

HRESULT CIsoMap::TestIsomValue(	__in const MapIsomData::IsomValue isomVal,
								__inout SearchNode *matchData )
{
	VERIFYARG( matchData );

	size_t numMatches = 0;
	for (size_t curDir=0;curDir<4;++curDir)
	{
		//	Does the neighbor value match the required neighbor value?
		if (matchData->neighborUnkVal[curDir] != this->isomMatchingData->isomDataTbl[isomVal * 13 + (curDir + 1) * 3])
		{
			//	If not and the neighbor already was updated to a new value,
			//	this isom value is definitely invalid
			if (matchData->neighborUpdated[curDir])
			{
				return S_FALSE;
			}
			continue;
		}


		//	Value appears to match.
		//	See if the isom group value also matches
		if (this->isomMatchingData->isomDataTbl[isomVal * 13 + (curDir + 1) * 3] >= 0xFF && // Appears to indicate some sort of 'Required exact match' for the isom search
			this->isomMatchingData->isomDataTbl[isomVal * 13] != this->isomMatchingData->isomDataTbl[matchData->neighborIsomVal[curDir] * 13] )
		{
			if (matchData->neighborUpdated[curDir])
			{
				return S_FALSE;
			}
		}
		else
		{
			++numMatches;
		}
	}

	if (numMatches > matchData->MatchCnt)
	{
		matchData->MatchCnt	= numMatches;
		matchData->IsomVal	= isomVal;
	}

	return S_OK;
}

HRESULT CIsoMap::SearchForMatch(	__in const TileCoordinate diamondX,
									__in const TileCoordinate diamondY,
									__in const DWORD undoID,
									__in CScmdraftUndo *undoList )
{
	HRESULT hr;

	SearchNode diamondMatchData;
	hr = this->PrepareSearchNode( diamondX, diamondY, &diamondMatchData );
	RETURNHRSILENT_IF_ERROR( hr );

	MapIsomData::IsomValue prevIsomVal = this->isomMatchingData->GetIsomValue( diamondX, diamondY );
	if (prevIsomVal * 13 >= this->isomMatchingData->isomDataTableLength)
		return E_FAIL;

	//	Make sure the node is only visited once.
	//	We reset this flag if surrounding nodes are changed, since it appears that sometimes the same node
	//	needs to be visited multiply times?
	if (this->isomMatchingData->GetIsomRect( diamondX, diamondY )->GetDirVisited( 0 ))
		return S_FALSE;
	this->isomMatchingData->GetIsomRect( diamondX, diamondY )->SetDirVisited( 0 );
	this->changedArea.left   = (std::min)(this->changedArea.left,   diamondX );
	this->changedArea.right  = (std::max)(this->changedArea.right,  diamondX );
	this->changedArea.top    = (std::min)(this->changedArea.top,    diamondY );
	this->changedArea.bottom = (std::max)(this->changedArea.bottom, diamondY );



	MapIsomData::IsomGroup prevIsomGroup = this->isomMatchingData->isomDataTbl[13 * prevIsomVal + 0];

	//	Three types of searches...
	MapIsomData::IsomGroup groupSearchStartVals[3];

	//	Search the group that connects the old value with the largest updated adjacent value
	//	(This should be the next step along the graph from source isom type to dest isom type)
	groupSearchStartVals[0] = this->isomMatchingData->matchPathCache[this->isomMatchingData->GetNumIsomValues() * diamondMatchData.maxGroupVal + prevIsomGroup];

	//	Search the current boundary type (or solid terrain type)
	groupSearchStartVals[1] = diamondMatchData.maxGroupVal;
	//	Not sure what type of search this is
	groupSearchStartVals[2] = this->isomMatchingData->GetNumIsomValues() / 2 + 1; // This may be == num solid terrain types
	
	for (size_t i=0;i<3;i++)
	{
		MapIsomData::IsomGroup searchStartGroup = groupSearchStartVals[i];
		MapIsomData::IsomValue curIsomVal = this->isomMatchingData->GetIsomVal( searchStartGroup );
		while (curIsomVal < this->isomMatchingData->isomDataTableLength)
		{
			//	See if we have started searching a different group.
			if (this->isomMatchingData->isomDataTbl[curIsomVal * 13] != searchStartGroup)
			{
				bool isSolidTerrain = false;
				//	XXX: Maybe: Are we running the last ditch search?
				if (searchStartGroup == this->isomMatchingData->GetNumIsomValues() / 2 + 1)
				{
					if (this->isomMatchingData->isomDataTbl[curIsomVal * 13] < searchStartGroup)
						isSolidTerrain = true;
				}
				if (! isSolidTerrain)
					if (searchStartGroup != 0)
						break;
			}

			hr = this->TestIsomValue( curIsomVal, &diamondMatchData );
			++curIsomVal;
		}
	}

	if (diamondMatchData.IsomVal != 0x00)
	{
		if (diamondMatchData.IsomVal == prevIsomVal)
		{
			return S_FALSE; // XXX: Should this set some alternative 'visited' flag?
		}

		hr = this->SetDiamondIsom(diamondX, diamondY, diamondMatchData.IsomVal, undoID, undoList);
		RETURNHRSILENT_IF_ERROR( hr );
	}

	for (size_t curDir=0;curDir<4;++curDir)
	{
		TileCoordinate neighborX = diamondX + diamondNeighborOffsets[curDir * 2 + 0];
		TileCoordinate neighborY = diamondY + diamondNeighborOffsets[curDir * 2 + 1];
		this->EnqueueTileUpdate( neighborX, neighborY );
	}

	return S_OK;
}




DWORD CIsoMap::GetTileHash(WORD X, WORD Y)
{
	return MakeHash( this->isomMatchingData->GetIsomRect( X, Y ) );
}



DWORD	CIsoMap::MakeHash( __in const MapIsomData::IsomRect *isomRect)
{
	DWORD borderValues[4] = {};
	MapIsomData::IsomGroup isomGroups[4] = {};
	for (size_t i=0;i<4;i++)
	{
		//	Note: first portion is the value in the direction table
		//	      Second portion is subthe index in the direction table
		//	      Final lookup is the actual isom value of the tile
		unsigned __int16 isomValue = isomRect->GetRawIsomValue(i);
		size_t dirIndex = (isomValue >> 2) & 0x03;
		size_t dirTableIndex = (isomValue >> 1) & 0x01;

		borderValues[i]	= this->isomMatchingData->isomDataTbl[(isomValue >> 4) * 13 + dirIndex * 3 + dirTableIndex + 1];
		isomGroups[i]	= this->isomMatchingData->isomDataTbl[(isomValue >> 4) * 13];
	}

	MapIsomData::IsomGroup isomGroup = 0;
	for (size_t i=0;i<4;i++)
	{
		if (borderValues[3 - i] < 0x30)
			continue;

		isomGroup = isomGroups[3 - i];
		if (isomGroup != 0)
			break;
	}

	DWORD isomDataHash = 0x00000000;
	for (size_t i=0;i<4;i++)
	{
		isomDataHash |= borderValues[i];
		isomDataHash <<= 6;
	}

	isomDataHash |= isomGroup;
	return isomDataHash;
}
