// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "CoreMinimal.h"
#include "VoxelSurfaceTypes.h"

class UWorld;

class FVoxelSurfaceDebug
{
public:
    // Call after ForceEnd from game thread. Draws lines/triangles for all surface chunks.
    static void DrawSurface(UWorld* World,
                            const TMap<FIntVector2, FVoxelSurfaceChunk>& Chunks,
                            int32 LayerFilter,   // -1 = all layers
                            float Duration);     // seconds, 0 = one frame
};
