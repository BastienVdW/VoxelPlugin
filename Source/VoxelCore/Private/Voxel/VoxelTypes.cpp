// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Voxel/VoxelTypes.h"

void FVoxelChunk::Init(FIntVector InCoord, int32 InCanonicalResolution, float InVoxelSize, int32 InBorderSize)
{
    ChunkCoord = InCoord;
    BorderSize = InBorderSize;
    Resolution = InCanonicalResolution + 2 * InBorderSize;
    VoxelSize  = InVoxelSize;
    bDirty     = true;
    bHasSolidVoxels = false;
    Voxels.SetNumZeroed(Resolution * Resolution * Resolution);
}

FVoxel& FVoxelChunk::At(int32 X, int32 Y, int32 Z)
{
    return Voxels[X + Y * Resolution + Z * Resolution * Resolution];
}

const FVoxel& FVoxelChunk::At(int32 X, int32 Y, int32 Z) const
{
    return Voxels[X + Y * Resolution + Z * Resolution * Resolution];
}

FVector FVoxelChunk::ChunkOriginWorld() const
{
    const int32 CanonicalR = Resolution - 2 * BorderSize;
    return FVector(ChunkCoord) * (CanonicalR * VoxelSize) - FVector(BorderSize * VoxelSize);
}
