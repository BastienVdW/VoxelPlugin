// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "VoxelSurfaceTypes.generated.h"

USTRUCT()
struct VOXELSURFACE_API FVoxelSurfaceLevel
{
	GENERATED_BODY()
	
	UPROPERTY()
	float   WorldZ     = 0.f;
	
	UPROPERTY()
	FVector Normal     = FVector::UpVector;
	
	UPROPERTY()
	int32   LayerIndex = 0;   // 0 = highest floor (rain/snow), ascending downward
};

USTRUCT()
struct VOXELSURFACE_API FVoxelSurfaceColumn
{
	GENERATED_BODY()
	
	UPROPERTY()
	FIntVector2                  ColumnCoord = FIntVector2::ZeroValue;
	
	UPROPERTY()
	TArray<FVoxelSurfaceLevel>   Levels;   // ordered top to bottom; empty = no solid floor found
};

USTRUCT()
struct VOXELSURFACE_API FVoxelSurfaceChunk
{
	GENERATED_BODY()
	
	UPROPERTY()
	FIntVector2                      ChunkCoord = FIntVector2::ZeroValue;
	
	UPROPERTY()
	TArray<FVoxelSurfaceColumn>      Columns;          // (Resolution+2)^2 columns including 1-col border
	
	UPROPERTY()
	bool                             bDirty = false;
};
