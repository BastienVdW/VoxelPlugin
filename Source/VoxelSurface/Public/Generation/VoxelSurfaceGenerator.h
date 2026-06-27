// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "CoreMinimal.h"
#include "VoxelSurfaceTypes.h"
#include "Async/TaskGraphInterfaces.h"

class FVoxelGrid;
struct FVoxelModifierData;

class VOXELSURFACE_API FVoxelSurfaceGenerator
{
public:
    // Dispatch one task per dirty surface chunk; writes results into OutChunks entries (pre-allocated by caller).
    void StartGeneration(const FVoxelGrid& Grid,
                         TArrayView<const FIntVector2> DirtyChunks,
                         TMap<FIntVector2, FVoxelSurfaceChunk>& OutChunks);

    // Blocks until all pending tasks complete.
    void ForceEndGeneration();

    bool IsGenerating() const;

    // Compute Z bounds from modifier bounds; returns default range if no modifiers.
    static void ComputeZBoundsFromModifiers(const TArray<FVoxelModifierData>& Modifiers,
                                            float DefaultSize,
                                            float& OutMinWorldZ, float& OutMaxWorldZ);

    // Thread-safe: scan one XY column through all Z, return all floor levels found.
    static FVoxelSurfaceColumn ScanColumn(
        float WorldX, float WorldY,
        float MinWorldZ, float MaxWorldZ,
        float StepSize, float SubFloorOffset,
        const TArray<FVoxelModifierData>& Modifiers);

private:
    // Binary search for zero-crossing between ZHigh (empty) and ZLow (solid).
    static float FindFloorZ(float WorldX, float WorldY,
                            float ZHigh, float ZLow,
                            const TArray<FVoxelModifierData>& Modifiers,
                            int32 MaxIterations = 8);

    // Central-difference normal estimate (step = StepEps).
    static FVector EstimateNormal(float WorldX, float WorldY, float WorldZ,
                                  const TArray<FVoxelModifierData>& Modifiers,
                                  float StepEps = 5.f);

    TArray<FIntVector2>      GeneratingChunks;
    FGraphEventArray         PendingEvents;
};
