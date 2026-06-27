// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "Modifier/VoxelModifierTypes.h"

namespace VoxelSDF
{
    // Squared distance from P to triangle ABC. Writes the closest point and barycentric weights
    // for B (OutU) and C (OutV); weight for A = 1 - OutU - OutV.
    VOXELSTREAMING_API float PointTriangleSqDist(
        const FVector& P,
        const FVector& A, const FVector& B, const FVector& C,
        FVector& OutClosest,
        float& OutU, float& OutV);

    // Möller–Trumbore ray-triangle intersection. Returns true and sets OutT if the ray hits.
    VOXELSTREAMING_API bool RayTriangle(
        const FVector& Orig, const FVector& Dir,
        const FVector& A, const FVector& B, const FVector& C,
        float& OutT);

    // Bakes a signed distance field from a closed triangle mesh given in local space.
    // SDF convention: negative inside, positive outside.
    // Writes SDFSamples, SDFResolution, SDFLocalBounds, and (if SourceUVs non-empty) UVSamples
    // into OutModifier. OutModifier.Transform must already be set before calling this.
    VOXELSTREAMING_API void BakeTriangleMeshSDF(
        const TArray<FVector>& LocalVerts,
        const TArray<int32>& Indices,
        const TArray<FVector2D>& SourceUVs,
        FVoxelModifierData& OutModifier,
        int32 Resolution = 32);
}
