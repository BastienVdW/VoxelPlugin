// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "HierarchicalHashGrid2D.h"
#include "Modifier/VoxelModifierTypes.h"

struct FVoxelModifierGridItem
{
    uint32 ModifierId = 0;
    FBox   Bounds3D   = FBox(EForceInit::ForceInit);

    bool operator==(const FVoxelModifierGridItem& O) const { return ModifierId == O.ModifierId; }
};

typedef THierarchicalHashGrid2D<4, 4, FVoxelModifierGridItem> FVoxelModifierHashGrid2D;

/**
 * Stores voxel modifiers spatially using a 2D hierarchical hash grid (XY plane) with
 * full 3D FBox intersection for precise per-chunk queries.
 *
 * Add/Remove: O(cells_touched) — fast for small/medium modifier counts.
 * HasModifiersAtChunk / GetModifiersForChunk: O(candidates) — early-exits where possible.
 * Remove rebuilds the 2D grid from scratch; acceptable since modifier changes are infrequent.
 */
class VOXELCORE_API FVoxelModifierGrid
{
public:
    void Add(uint32 Id, const FVoxelModifierData& Data);
    void Remove(uint32 Id, const FVoxelModifierData& Data);

    const FVoxelModifierData* GetById(uint32 Id) const;

    /** Returns true if any modifier's 3D bounds intersect the chunk at ChunkCoord. */
    bool HasModifiersAtChunk(FIntVector ChunkCoord, float ChunkWorldSize) const;

    /**
     * Returns all modifier data whose 3D bounds intersect the chunk (expanded by BorderWorldSize
     * on all sides, matching the border-voxel ring used during baking).
     */
    TArray<FVoxelModifierData> GetModifiersForChunk(
		FIntVector ChunkCoord, float ChunkWorldSize, float BorderWorldSize) const;
	TArray<FVoxelModifierGridItem> GetModifierItemsForChunk(
		FIntVector ChunkCoord, float ChunkWorldSize, float BorderWorldSize) const;

    /**
     * Returns all chunk coords (at ChunkWorldSize granularity) that lie within WorldBox3D
     * AND have at least one modifier overlapping them.
     */
    TSet<FIntVector> GetChunksWithModifiersInRegion(const FBox& WorldBox3D, float ChunkWorldSize) const;

    /** Returns handles of all modifiers whose 3D bounds intersect WorldBox3D. */
    TArray<FModifierHandle> GetModifierHandlesInBounds(const FBox& WorldBox3D) const;

    /**
     * Returns all modifiers whose XY bounds overlap the column at ColumnCoord,
     * using the 2D spatial index for efficient lookup.
     */
    TArray<FVoxelModifierData> GetModifiersAtXY(FIntVector2 ColumnCoord, float ChunkWorldSize, float BorderWorldSize) const;

private:
    /** Spatial index: items registered by XY footprint. */
    FVoxelModifierHashGrid2D Grid2D;

    /** Full data by modifier ID. */
    TMap<uint32, FVoxelModifierData> DataById;

    /** Rebuild Grid2D from DataById. Called after every Remove. */
    void RebuildGrid();

    /** Returns the XY-only FBox used for Grid2D insertion/query. */
    static FBox ToXYBounds(const FBox& Box3D);

    /** Returns the 3D chunk box (no border). */
    static FBox ChunkBox(FIntVector ChunkCoord, float ChunkWorldSize);

    /** Returns the 3D chunk box expanded by border on all sides. */
    static FBox ChunkBoxWithBorder(FIntVector ChunkCoord, float ChunkWorldSize, float BorderWorldSize);

    /** Dedupes query results by modifier ID and returns them in deterministic ID order. */
    TArray<FVoxelModifierGridItem> GetSortedModifierItemsInBounds(
        const FBox& WorldBox3D, bool bRequire3DIntersection = true) const;
};
