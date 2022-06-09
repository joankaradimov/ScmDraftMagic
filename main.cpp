#include <stdio.h>
#include <vector>
#include <iostream>
#include <iomanip>
#include <map>
#include <fstream>
#include <Windows.h>
#include "CIsoTables.h"

struct TileGroupRaw
{
  BYTE        terrainType;
  BYTE        UNK1;
  WORD        flags;
  WORD        primaryMatch[4];
  WORD        intraGroupMatching[4];
  WORD        tileIDs[16];
};

static_assert(sizeof(TileGroupRaw) == 52, "The size of a CV5 entry is 52 bytes");

std::vector<TileGroupRaw> ReadTiles(const char* cv5Path)
{
  std::ifstream file(cv5Path, std::ios::binary | std::ios::ate);
  std::streamsize fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<TileGroupRaw> result(fileSize / sizeof(TileGroupRaw));
  if (file.read((char*)result.data(), fileSize))
  {
    return result;
  }

  throw "Could not read tileset data";
}

struct TileGroupsSpec {
    void registerTerrainType(BYTE terrainType)
    {
        maxTerrainType = max(maxTerrainType, terrainType);

        if (terrainTypes.size() == 0 || terrainTypes.back() != terrainType)
        {
            terrainTypes.push_back(terrainType);
        }
    }

    BYTE maxTerrainType;
    DWORD firstNullTerrainTypeIndex;
    std::vector<BYTE> terrainTypes;
};

bool IsNullTile(const TileGroupRaw& tileGroup)
{
    return tileGroup.terrainType == 0;
}

bool IsDoodadTile(const TileGroupRaw& tileGroup)
{
    return tileGroup.terrainType == 1;
}

bool IsPrimaryTile(const TileGroupRaw& tileGroup)
{
    return
        tileGroup.primaryMatch[0] == tileGroup.primaryMatch[1] &&
        tileGroup.primaryMatch[1] == tileGroup.primaryMatch[2] &&
        tileGroup.primaryMatch[2] == tileGroup.primaryMatch[3];
}

TileGroupsSpec GetTilesetGroupSpecs(const std::vector<TileGroupRaw>& tileGroups)
{
    TileGroupsSpec result;
    result.maxTerrainType = 0;
    result.firstNullTerrainTypeIndex = 0;

    int index = 0;

    for (; index < tileGroups.size() && IsPrimaryTile(tileGroups[index]); index++)
    {
        if (IsNullTile(tileGroups[index]))
        {
            if (result.firstNullTerrainTypeIndex == 0)
            {
                result.firstNullTerrainTypeIndex = index / 2;
            }
            continue;
        }

        if (IsDoodadTile(tileGroups[index]))
        {
            break;
        }

        result.registerTerrainType(tileGroups[index].terrainType);
    }

    for (; index < tileGroups.size(); index++)
    {
        if (IsNullTile(tileGroups[index]))
        {
            continue;
        }

        if (IsDoodadTile(tileGroups[index]))
        {
            break;
        }

        result.registerTerrainType(tileGroups[index].terrainType);
    }

    return result;
}

int main()
{
  std::vector<TileGroupRaw> ashworldGroupRaw = ReadTiles("D:\\dev\\work\\ScmDraftTables\\tileset\\ashworld.cv5");
  std::vector<TileGroupRaw> badlandsGroupRaw = ReadTiles("D:\\dev\\work\\ScmDraftTables\\tileset\\badlands.cv5");
  std::vector<TileGroupRaw> installGroupRaw = ReadTiles("D:\\dev\\work\\ScmDraftTables\\tileset\\install.cv5");
  std::vector<TileGroupRaw> jungleGroupRaw = ReadTiles("D:\\dev\\work\\ScmDraftTables\\tileset\\jungle.cv5");
  std::vector<TileGroupRaw> platformGroupRaw = ReadTiles("D:\\dev\\work\\ScmDraftTables\\tileset\\platform.cv5");

  printf("Ash tile group count %lld\n", ashworldGroupRaw.size());
  printf("Badlands tile group count %lld\n", badlandsGroupRaw.size());
  printf("Installation tile group count %lld\n", installGroupRaw.size());
  printf("Jungle tile group count %lld\n", jungleGroupRaw.size());
  printf("Platform tile group count %lld\n", platformGroupRaw.size());

  TileGroupsSpec spec = GetTilesetGroupSpecs(platformGroupRaw);

  DWORD isomData[] = { 0 };
  DWORD matchTbl[] = { 0 };
  DWORD* indexToIsom = (DWORD*) malloc(spec.maxTerrainType + 1);
  indexToIsom[0] = spec.firstNullTerrainTypeIndex;
  indexToIsom[1] = 0;

  const DWORD* JungleTileset[] = {
      isomData,
      matchTbl,
      indexToIsom,
      (DWORD*)_countof(isomData),
      (DWORD*)(1 + spec.maxTerrainType),
  };

  std::cout << (int)spec.maxTerrainType << std::endl;

  return 0;
}
