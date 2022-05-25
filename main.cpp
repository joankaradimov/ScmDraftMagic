#include <stdio.h>
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

int main()
{
  return 0;
}
