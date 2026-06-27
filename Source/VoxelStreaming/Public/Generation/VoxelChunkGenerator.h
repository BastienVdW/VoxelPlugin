// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "Async/TaskGraphInterfaces.h"

class FVoxelGrid;

// Async parallel chunk baking. Not thread-safe across StartGeneration calls.
// Always call ForceEndGeneration before the next StartGeneration.
class VOXELSTREAMING_API FVoxelChunkGenerator
{
public:
    // Enqueue one async task per dirty chunk. Non-blocking.
    void StartGeneration(const UWorld* World, FVoxelGrid& Grid, TArrayView<const FIntVector> DirtyChunks);

    // Block until all enqueued tasks complete. Game-thread safe.
    void ForceEndGeneration(const UWorld* World, const FVoxelGrid& Grid);

    bool IsGenerating() const;

    static void BakeChunk(FVoxelGrid& Grid, FIntVector ChunkCoord);

    // Exposed as public for unit testing — trilinear sample into a baked SDF grid.
    static float SampleSDFTrilinear(const struct FVoxelModifierData& M, const FVector& LocalPos);

    // Trilinear sample into a baked UV grid. Returns ZeroVector if no UV data.
    static FVector2f SampleUVTrilinear(const struct FVoxelModifierData& M, const FVector& LocalPos);

    // Exposed as public for unit testing — blends all modifier densities at a world position.
    static float EvaluateDensity(const FVector& WorldPos, const TArray<struct FVoxelModifierData>& Modifiers);
	
private:
    FGraphEventArray PendingEvents;
	TArray<FIntVector> GeneratingChunks;
};
