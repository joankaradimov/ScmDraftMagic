#include <stdio.h>
#include <vector>
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

  return 0;
}
