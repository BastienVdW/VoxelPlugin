// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VoxelSurfaceTypes.h"
#include "Generation/VoxelSurfaceGenerator.h"

#include "VoxelSurfaceSubsystem.generated.h"

class FVoxelGrid;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnVoxelSurfaceFlushed, const TArray<FIntVector2>& /*UpdatedChunks*/);

UCLASS()
class VOXELSURFACE_API UVoxelSurfaceSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // Called at PrePhysics (parallel with voxel chunk baking).
    // Projects dirty 3D chunk XY coords into surface chunk coords, dispatches tasks.
    void StartSurfaceGeneration(const FVoxelGrid& Grid, const TArray<FIntVector>& DirtyChunks);

    // Called at FrameEnd (VoxelSurfaceFlush group, after VoxelFlush).
    // Blocks on tasks, then stitches 1-col border seams between neighboring chunks.
    void ForceEndSurfaceGeneration();

    // Draws debug visualization of surface chunks if enabled via cvar.
    void DrawDebug() const;

    FOnVoxelSurfaceFlushed OnSurfaceFlushed;

    const FVoxelSurfaceChunk* GetSurfaceChunk(FIntVector2 ChunkCoord) const;

    /** Returns existing chunks intersecting the world-space XY bounds. */
    TArray<FIntVector2> GetSurfaceChunkCoordsInBounds(const FBox2D& WorldBounds) const;
    const TMap<FIntVector2, FVoxelSurfaceChunk>& GetAllSurfaceChunks() const { return SurfaceChunks; }

    // Recall restore path: directly overwrites the chunk map so landscape reads coherent
    // heights immediately after a rollback. Rendered meshes are not touched — the normal
    // async dirty-chunk generation will rebuild them in the next frame.
    void SetAllSurfaceChunks(const TMap<FIntVector2, FVoxelSurfaceChunk>& Chunks) { SurfaceChunks = Chunks; }

private:
    void StitchSeams(TArrayView<const FIntVector2> UpdatedChunks);

	UPROPERTY(Transient)
    TMap<FIntVector2, FVoxelSurfaceChunk> SurfaceChunks;
	
    FVoxelSurfaceGenerator                Generator;
    TArray<FIntVector2>                   LastDirtyChunks;
};
