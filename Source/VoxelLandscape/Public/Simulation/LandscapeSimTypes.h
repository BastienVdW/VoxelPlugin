// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "VoxelLandscapeTypes.h"
#include "Settings/VoxelDeveloperSettings.h"

struct FLandscapeSimInput
{
	TArray<FLandscapeCell>  Cells;        // (Size)^2 including border; input copy
	TArray<float>           SurfaceZ;     // WorldZ per cell index, same layout as Cells
	FLandscapeLayerParams   Params;
	int32                   GridSize;     // ColRes = Resolution+2
	// Border cells are copied from neighbor chunks before simulation. They are read as receivers
	// for outgoing flow, but must not emit fluid themselves or the copied neighbor depth is duplicated.
	bool                    bUseGhostBorder = false;
	FName                   LayerName;
	FIntVector2             ChunkCoord;
	int32                   SurfaceLayerIndex;
};

struct FLandscapeSimOutput
{
	TArray<FLandscapeCell>  Cells;
	FName                   LayerName;
	FIntVector2             ChunkCoord;
	int32                   SurfaceLayerIndex;
};
