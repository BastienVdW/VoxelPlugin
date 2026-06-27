// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "VoxelLandscapeTypes.generated.h"

USTRUCT()
struct VOXELLANDSCAPE_API FLandscapeCell
{
    GENERATED_BODY()
	
	// depth/quantity >= 0; 0 = absent
	UPROPERTY()
    float Depth = 0.f;

	// Horizontal flow velocity in landscape cell units per simulation step.
	UPROPERTY()
	FVector2f Velocity = FVector2f::ZeroVector;
};

USTRUCT()
struct VOXELLANDSCAPE_API FVoxelLandscapeLayer
{
    GENERATED_BODY()
	
	// Depth and velocity for each cell in the surface layer. Cells are stored in column-major order (X changes fastest).
    UPROPERTY()
	TArray<FLandscapeCell> Cells;
};

USTRUCT()
struct FLandscapeChunkKey
{
	GENERATED_BODY()
	
	// XY coordinate of the chunk in landscape grid space. Each chunk contains one or more surface layers.
	UPROPERTY()
    FIntVector2 ChunkCoord = FIntVector2::ZeroValue;
	
	// Index of the surface layer that this chunk represents. Chunks with the same XY coordinate but different surface layers are distinct.
	UPROPERTY()
    int32       SurfaceLayerIndex = 0;

    bool operator==(const FLandscapeChunkKey& Other) const
    {
        return ChunkCoord == Other.ChunkCoord && SurfaceLayerIndex == Other.SurfaceLayerIndex;
    }
};

inline uint32 GetTypeHash(const FLandscapeChunkKey& Key)
{
    return HashCombine(GetTypeHash(Key.ChunkCoord), GetTypeHash(Key.SurfaceLayerIndex));
}

USTRUCT()
struct VOXELLANDSCAPE_API FVoxelLandscapeChunk
{
	GENERATED_BODY()
	
	// XY coordinate of the chunk in landscape grid space. Each chunk contains one or more surface layers.
	UPROPERTY()
    FIntVector2 ChunkCoord = FIntVector2::ZeroValue;
	
	// Index of the surface layer that this chunk represents. Chunks with the same XY coordinate but different surface layers are distinct.
	UPROPERTY()
    int32       SurfaceLayerIndex = 0;
	
    // Key = layer name ("Water", "Snow", ...); depth wraps (Resolution+2)^2 cells, column-major
	UPROPERTY()
    TMap<FName, FVoxelLandscapeLayer> Layers;
	
	// Tracks whether this chunk has been modified since the last simulation step. Used to schedule simulation and rendering.
    UPROPERTY()
    bool bDirty = false;

	// Consecutive simulation steps below the configured activity thresholds.
	UPROPERTY()
	int32 StableSimulationSteps = 0;

	// Settled chunks retain their fluid data but are skipped by simulation until woken.
	UPROPERTY()
	bool bSleeping = false;
};

// Complete persistent state of a landscape grid. Runtime lookup indexes are rebuilt from this data.
USTRUCT()
struct VOXELLANDSCAPE_API FVoxelLandscapeGridData
{
	GENERATED_BODY()

	// Chunks are keyed by (ChunkCoord, SurfaceLayerIndex). Each chunk contains one or more layers keyed by name.
	UPROPERTY()
	TMap<FLandscapeChunkKey, FVoxelLandscapeChunk> Chunks;

	// Dirty work can be produced after the frame flush and must survive a snapshot boundary.
	UPROPERTY()
	TSet<FLandscapeChunkKey> DirtyKeys;
	
	FORCEINLINE void Reset()
	{
		Chunks.Reset();
		DirtyKeys.Reset();
	}
};
