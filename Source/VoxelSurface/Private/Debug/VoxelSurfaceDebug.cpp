// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Debug/VoxelSurfaceDebug.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"

static FLinearColor LayerIndexToColor(int32 LayerIndex)
{
    switch (LayerIndex % 4)
    {
    case 0:  return FLinearColor::Green;
    case 1:  return FLinearColor::Blue;
    case 2:  return FLinearColor::Yellow;
    default: return FLinearColor::Red;
    }
}

/*static*/ void FVoxelSurfaceDebug::DrawSurface(UWorld* World,
                                                 const TMap<FIntVector2, FVoxelSurfaceChunk>& Chunks,
                                                 int32 LayerFilter,
                                                 float Duration)
{
    if (!World) return;

    for (const auto& [Coord, Chunk] : Chunks)
    {
        for (const FVoxelSurfaceColumn& Col : Chunk.Columns)
        {
            for (const FVoxelSurfaceLevel& Level : Col.Levels)
            {
                if (LayerFilter != -1 && Level.LayerIndex != LayerFilter) continue;

                const FColor Color = LayerIndexToColor(Level.LayerIndex).ToFColor(true);
                const FVector Pos(
                    Coord.X * 400.f + Col.ColumnCoord.X * 25.f,
                    Coord.Y * 400.f + Col.ColumnCoord.Y * 25.f,
                    Level.WorldZ);

                DrawDebugPoint(World, Pos, 4.f, Color, false, Duration);
                DrawDebugLine(World, Pos, Pos + Level.Normal * 15.f, Color, false, Duration, 0, 1.f);
            }
        }
    }
}
