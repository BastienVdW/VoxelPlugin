// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"
#include "Modifier/VoxelModifierTypes.h"
#include "Grid/VoxelModifierGrid.h"

struct VOXELCORE_API FVoxelGridConfig
{
    int32 DefaultResolution = 16;
    float DefaultVoxelSize  = 25.f;
    int32 DefaultBorderSize = 1;
};

class VOXELCORE_API FVoxelGrid
{
public:
    explicit FVoxelGrid(FVoxelGridConfig InConfig = {});
    ~FVoxelGrid() = default;

    // Modifier management — game thread only
    FModifierHandle           AddModifier(const FVoxelModifierData& Modifier, uint32 HandleId = 0);
    void                      RemoveModifier(FModifierHandle& Handle);
    const FVoxelModifierData* GetModifier(const FModifierHandle& Handle) const;

    // Returns true if any modifier overlaps this chunk
    bool HasModifiersAtChunk(FIntVector ChunkCoord) const;

    // Chunk access
    FVoxelChunk*       GetOrCreateChunk(FIntVector ChunkCoord);
    FVoxelChunk*       FindChunk(FIntVector ChunkCoord);
    const FVoxelChunk* QueryChunk(FIntVector ChunkCoord) const;
    void               EvictChunk(FIntVector ChunkCoord);

    FVoxel GetVoxel(FVector WorldPos) const;

    TArray<FIntVector> GetDirtyChunks() const;
    void               ClearDirtyFlag(FIntVector ChunkCoord);

    TArray<FVoxelModifierData> GetModifiersForChunk(const FIntVector& ChunkCoord) const;
    TArray<FModifierHandle>    GetModifierHandlesForChunk(const FIntVector& ChunkCoord) const;
    TArray<FModifierHandle>    GetModifierHandlesInBounds(const FBox& WorldBox3D) const;
    TArray<FIntVector>         GetAllChunkCoords() const;

    /**
     * Returns the chunk coords within Radius of Position that have at least one overlapping modifier.
     * Much cheaper than iterating all chunks in the sphere when modifiers are sparse.
     */
    TSet<FIntVector> GetChunksWithModifiersInSphere(const FVector& Position, float Radius) const;

    FIntVector WorldToChunkCoord(FVector WorldPos) const;
    FIntVector WorldToLocalVoxel(FVector WorldPos, FIntVector ChunkCoord) const;
	
    const FVoxelGridConfig& GetConfig() const { return Config; }
    const FVoxelModifierGrid& GetModifierGrid() const { return ModifierGrid; }

	// Modifier ID management — used by URecallVoxelSubsystem to assign stable IDs to dynamic modifiers
	uint32 GetNextModifierId() const { return NextModifierId; }
	void SetNextModifierId(uint32 NewId) { NextModifierId = NewId; }

    // Called by UVoxelStreamingSubsystem around async generation to guard modifier mutations.
    void SetGenerating(bool bGenerating) { bIsGenerating = bGenerating; }
    bool IsGenerating() const { return bIsGenerating; }

private:
    void MarkChunksOverlappingBounds(const FBoxSphereBounds& Bounds);

    FVoxelGridConfig Config;

    TMap<FIntVector, TSharedPtr<FVoxelChunk>> Chunks;
    TSet<FIntVector> DirtyChunkCoords;

    FVoxelModifierGrid ModifierGrid;
    uint32 NextModifierId = 1;
    bool bIsGenerating = false;
};
