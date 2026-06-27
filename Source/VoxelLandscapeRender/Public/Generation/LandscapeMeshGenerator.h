// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "CoreMinimal.h"
#include "VoxelLandscapeTypes.h"
#include "VoxelSurfaceTypes.h"

// Output of ComputeSection — pure data, safe to construct on any thread.
struct FLandscapeLayerSection
{
    TArray<FVector>   Vertices;
    TArray<int32>     Indices;
    TArray<FVector>   Normals;
    TArray<FVector2D> UVs;
};

class VOXELLANDSCAPERENDER_API FLandscapeMeshGenerator
{
public:
    // Pure computation — no UObject access, safe on any thread.
    // LocalOffset is subtracted from vertex XY so the result is in actor-local space.
    static FLandscapeLayerSection ComputeSection(
        const FVoxelLandscapeChunk& Chunk,
        const FVoxelSurfaceChunk&   Surface,
        FName                        LayerName,
        float                        VoxelSize,
        FVector2D                    LocalOffset = FVector2D::ZeroVector);

private:
    static bool IsRenderableCell(const TArray<bool>& CellWet, int32 ColRes, int32 CellX, int32 CellY);
    static float ComputeTopVertexZ(
        const TArray<float>& CellSurfZ,
        const TArray<float>& CellLevel,
        const TArray<bool>& CellWet,
        int32 ColRes,
        int32 VertexX,
        int32 VertexY,
        bool bHasWetCell,
        float MinWetLevel);
    static float ComputeSkirtFloorZ(
        const TArray<float>& CellSurfZ,
        int32 ColRes,
        int32 VertexX,
        int32 VertexY,
        float SkirtDepth);
};
