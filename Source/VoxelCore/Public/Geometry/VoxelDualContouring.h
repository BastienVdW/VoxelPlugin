// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once
#include "CoreMinimal.h"
#include "Geometry/VoxelMarchingCubes.h" // FVoxelMeshData
#include "Voxel/VoxelTypes.h"            // FVoxelChunk

class VOXELCORE_API FVoxelDualContouring
{
public:
    // Chunk.Resolution must be CanonicalR + 2*Chunk.BorderSize.
    // Produces mesh covering the canonical CanonicalR^3 cells.
    static FVoxelMeshData ExtractSurface(const FVoxelChunk& Chunk);

private:
    struct FQEFData
    {
        float     ATA[6] = {}; // symmetric 3x3: [0,0],[0,1],[0,2],[1,1],[1,2],[2,2]
        float     ATb[3] = {};
        FVector3f PointAccum = FVector3f::ZeroVector;
        int32     Count = 0;
        uint16    MaterialHash = 0;
        FVector2f UV = FVector2f::ZeroVector;
    };

    static FVector3f GradientAt(const FVoxelChunk& Chunk, int32 X, int32 Y, int32 Z);
    static bool      Solve3x3(float A[3][3], float b[3], float out[3]);
};
