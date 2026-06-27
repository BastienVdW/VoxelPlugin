// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once
#include "CoreMinimal.h"
#include "Geometry/VoxelMarchingCubes.h" // reuses FVoxelMeshData
#include "Voxel/VoxelTypes.h"

// Produces a perfectly cubic (Minecraft-style) mesh by merging coplanar voxel faces.
// MeshSmoothing is ignored — output is always axis-aligned quads.
class VOXELCORE_API FVoxelGreedyMesh
{
public:
    static FVoxelMeshData ExtractSurface(const FVoxelChunk& Chunk);
};
