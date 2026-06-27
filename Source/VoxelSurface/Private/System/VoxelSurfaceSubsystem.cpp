// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "System/VoxelSurfaceSubsystem.h"
#include "Grid/VoxelGrid.h"
#include "Debug/VoxelSurfaceDebug.h"
#include "Settings/VoxelDeveloperSettings.h"

static int32 GVoxelSurfaceDebugEnabled = 0;
static int32 GVoxelSurfaceDebugLayer   = -1;

static FAutoConsoleVariableRef CVarSurfaceDebug(
    TEXT("VoxelSurface.Debug"),
    GVoxelSurfaceDebugEnabled,
    TEXT("Draw VoxelSurface debug visualization (0=off, 1=on)"));

static FAutoConsoleVariableRef CVarSurfaceDebugLayer(
    TEXT("VoxelSurface.Debug.Layer"),
    GVoxelSurfaceDebugLayer,
    TEXT("Layer index to draw (-1 = all)"));

void UVoxelSurfaceSubsystem::StartSurfaceGeneration(const FVoxelGrid& Grid, const TArray<FIntVector>& DirtyChunks)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(TEXT("UVoxelSurfaceSubsystem::StartDirtySurfaceGeneration"));
	
	checkf(!Generator.IsGenerating(), TEXT("StartDirtySurfaceGeneration called while previous generation is still in progress!"));
	
    // Project dirty 3D chunk coords → unique 2D surface coords
    TSet<FIntVector2> DirtySet;
    for (const FIntVector& Chunk : DirtyChunks)
    {
        DirtySet.Add(FIntVector2(Chunk.X, Chunk.Y));
    }

    LastDirtyChunks = DirtySet.Array();

    // Pre-allocate surface chunk entries on game thread (mirrors voxel GetOrCreateChunk pattern)
    for (const FIntVector2& Coord : LastDirtyChunks)
    {
        FVoxelSurfaceChunk& Chunk = SurfaceChunks.FindOrAdd(Coord);
        Chunk.ChunkCoord          = Coord;
        Chunk.bDirty              = true;
        Chunk.Columns.Reset();
    }

    Generator.StartGeneration(Grid, LastDirtyChunks, SurfaceChunks);
}

void UVoxelSurfaceSubsystem::ForceEndSurfaceGeneration()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(TEXT("UVoxelSurfaceSubsystem::ForceEndSurfaceGeneration"));
	
    Generator.ForceEndGeneration();
    StitchSeams(LastDirtyChunks);

    for (const FIntVector2& Coord : LastDirtyChunks)
    {
        if (FVoxelSurfaceChunk* Chunk = SurfaceChunks.Find(Coord))
        {
            Chunk->bDirty = false;
        }
    }
    OnSurfaceFlushed.Broadcast(LastDirtyChunks);
	LastDirtyChunks.Reset();
}

void UVoxelSurfaceSubsystem::DrawDebug() const
{
    if (GVoxelSurfaceDebugEnabled)
    {
        FVoxelSurfaceDebug::DrawSurface(GetWorld(), SurfaceChunks, GVoxelSurfaceDebugLayer, 0.f);
    }
}

const FVoxelSurfaceChunk* UVoxelSurfaceSubsystem::GetSurfaceChunk(FIntVector2 ChunkCoord) const
{
    return SurfaceChunks.Find(ChunkCoord);
}

TArray<FIntVector2> UVoxelSurfaceSubsystem::GetSurfaceChunkCoordsInBounds(
    const FBox2D& WorldBounds) const
{
    const UVoxelDeveloperSettings* Settings = GetDefault<UVoxelDeveloperSettings>();
    const float ChunkWorldSize = Settings->ChunkResolution * Settings->VoxelSize;
    const FIntVector2 MinChunk(
        FMath::FloorToInt(WorldBounds.Min.X / ChunkWorldSize),
        FMath::FloorToInt(WorldBounds.Min.Y / ChunkWorldSize));
    const FIntVector2 MaxChunk(
        FMath::FloorToInt(WorldBounds.Max.X / ChunkWorldSize),
        FMath::FloorToInt(WorldBounds.Max.Y / ChunkWorldSize));

    TArray<FIntVector2> Result;
    for (int32 Y = MinChunk.Y; Y <= MaxChunk.Y; ++Y)
    {
        for (int32 X = MinChunk.X; X <= MaxChunk.X; ++X)
        {
            const FIntVector2 ChunkCoord(X, Y);
            if (SurfaceChunks.Contains(ChunkCoord))
            {
                Result.Add(ChunkCoord);
            }
        }
    }
    return Result;
}

void UVoxelSurfaceSubsystem::StitchSeams(TArrayView<const FIntVector2> UpdatedChunks)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(TEXT("UVoxelSurfaceSubsystem::StitchSeams"));
	
    // Resolution+2 per axis (includes 1-col border)
    const int32 ColRes = 18; // 16 + 2

    // Directions: right (+X), up (+Y)
    const FIntVector2 Dirs[2] = { FIntVector2(1, 0), FIntVector2(0, 1) };

    for (const FIntVector2& Coord : UpdatedChunks)
    {
        FVoxelSurfaceChunk* Chunk = SurfaceChunks.Find(Coord);
        if (!Chunk || Chunk->Columns.Num() != ColRes * ColRes) continue;

        for (const FIntVector2& Dir : Dirs)
        {
            FVoxelSurfaceChunk* Neighbor = SurfaceChunks.Find(Coord + Dir);
            if (!Neighbor || Neighbor->Columns.Num() != ColRes * ColRes) continue;

            if (Dir.X == 1)
            {
                // Right border of Chunk (col index ColRes-1) ↔ Left border of Neighbor (col index 0)
                for (int32 cy = 0; cy < ColRes; ++cy)
                {
                    FVoxelSurfaceColumn& BorderCol  = Chunk->Columns[(ColRes - 1) + cy * ColRes];
                    FVoxelSurfaceColumn& InnerCol   = Neighbor->Columns[1 + cy * ColRes];
                    // Snap border to neighbor's inner (neighbor already computed its own border)
                    BorderCol.Levels = InnerCol.Levels;
                }
            }
            else // Dir.Y == 1
            {
                // Top border of Chunk (row ColRes-1) ↔ Bottom border of Neighbor (row 0)
                for (int32 cx = 0; cx < ColRes; ++cx)
                {
                    FVoxelSurfaceColumn& BorderCol = Chunk->Columns[cx + (ColRes - 1) * ColRes];
                    FVoxelSurfaceColumn& InnerCol  = Neighbor->Columns[cx + 1 * ColRes];
                    BorderCol.Levels = InnerCol.Levels;
                }
            }
        }
    }
}
