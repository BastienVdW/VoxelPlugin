// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Generation/VoxelSurfaceGenerator.h"

#include "Async/ParallelFor.h"
#include "Generation/VoxelChunkGenerator.h"
#include "Grid/VoxelGrid.h"
#include "Modifier/VoxelModifierTypes.h"
#include "Settings/VoxelDeveloperSettings.h"

// ──────────────────────────────────────────────────────────────────────────────
// Task
// ──────────────────────────────────────────────────────────────────────────────
class FVoxelSurfaceColumnTask
{
public:
    FVoxelSurfaceColumnTask(FVoxelSurfaceChunk* InChunk,
                            TArray<FVoxelModifierData> InModifiers,
                            float InStepSize,  float InSubFloorOffset,
                            float InChunkWorldSize)
        : Chunk(InChunk)
        , Modifiers(MoveTemp(InModifiers))
        , StepSize(InStepSize),   SubFloorOffset(InSubFloorOffset)
        , ChunkWorldSize(InChunkWorldSize)
    {}

    static TStatId GetStatId()
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FVoxelSurfaceColumnTask, STATGROUP_TaskGraphTasks);
    }
    static ENamedThreads::Type GetDesiredThread()
    {
        return ENamedThreads::AnyBackgroundThreadNormalTask;
    }
    static ESubsequentsMode::Type GetSubsequentsMode()
    {
        return ESubsequentsMode::TrackSubsequents;
    }

    void DoTask(ENamedThreads::Type, const FGraphEventRef&)
    {    	
        float MinWorldZ, MaxWorldZ;
        FVoxelSurfaceGenerator::ComputeZBoundsFromModifiers(Modifiers, ChunkWorldSize, MinWorldZ, MaxWorldZ);

        // Resolution+2 columns per axis (1-col border on each side)
    	const UVoxelDeveloperSettings* VoxelSettings = GetDefault<UVoxelDeveloperSettings>();
        const int32 VoxelRes   = VoxelSettings->ChunkResolution;
        const int32 ColRes     = VoxelRes + 2;
        const float VoxelSize  = ChunkWorldSize / VoxelRes;
        const float OriginX    = Chunk->ChunkCoord.X * ChunkWorldSize - VoxelSize;
        const float OriginY    = Chunk->ChunkCoord.Y * ChunkWorldSize - VoxelSize;

        Chunk->Columns.SetNum(ColRes * ColRes);

    	ParallelFor(ColRes * ColRes, [&](int32 Index)
    	{
    		const int32 cy = Index / ColRes;
    		const int32 cx = Index % ColRes;
    			
    		const float WorldX = OriginX + cx * VoxelSize;
			const float WorldY = OriginY + cy * VoxelSize;

			FVoxelSurfaceColumn& Col = Chunk->Columns[Index];
			Col                      = FVoxelSurfaceGenerator::ScanColumn(
				WorldX, WorldY, MinWorldZ, MaxWorldZ, StepSize, SubFloorOffset, Modifiers);
			Col.ColumnCoord          = FIntVector2(cx, cy);
    	});
    }

private:
    FVoxelSurfaceChunk*        Chunk;
    TArray<FVoxelModifierData> Modifiers;
    float                      StepSize, SubFloorOffset, ChunkWorldSize;
};

// ──────────────────────────────────────────────────────────────────────────────
// FVoxelSurfaceGenerator
// ──────────────────────────────────────────────────────────────────────────────
void FVoxelSurfaceGenerator::StartGeneration(const FVoxelGrid& Grid,
                                              TArrayView<const FIntVector2> DirtyChunks,
                                              TMap<FIntVector2, FVoxelSurfaceChunk>& OutChunks)
{
    ForceEndGeneration();
    GeneratingChunks = TArray<FIntVector2>(DirtyChunks.GetData(), DirtyChunks.Num());

    const FVoxelGridConfig& Cfg   = Grid.GetConfig();
    const float ChunkWorldSize    = Cfg.DefaultResolution * Cfg.DefaultVoxelSize;
    const float BorderWorldSize   = Cfg.DefaultBorderSize * Cfg.DefaultVoxelSize;

	const UVoxelDeveloperSettings* VoxelSettings = GetDefault<UVoxelDeveloperSettings>();
    const float StepSize       = Cfg.DefaultVoxelSize * 0.5f;
    const float SubFloorOffset = VoxelSettings->SubFloorOffset;

    PendingEvents.Reset();
    for (const FIntVector2& SurfaceCoord : GeneratingChunks)
    {
        FVoxelSurfaceChunk* Chunk = OutChunks.Find(SurfaceCoord);
        if (!Chunk) continue;

        TArray<FVoxelModifierData> Modifiers = Grid.GetModifierGrid().GetModifiersAtXY(
            SurfaceCoord, ChunkWorldSize, BorderWorldSize);

        FGraphEventRef Event = TGraphTask<FVoxelSurfaceColumnTask>::CreateTask(nullptr)
            .ConstructAndDispatchWhenReady(Chunk, MoveTemp(Modifiers),
                StepSize, SubFloorOffset, ChunkWorldSize);
        PendingEvents.Add(Event);
    }
}

void FVoxelSurfaceGenerator::ForceEndGeneration()
{
    if (!PendingEvents.IsEmpty())
    {
        FTaskGraphInterface::Get().WaitUntilTasksComplete(PendingEvents);
        PendingEvents.Reset();
    }
    GeneratingChunks.Reset();
}

void FVoxelSurfaceGenerator::ComputeZBoundsFromModifiers(const TArray<FVoxelModifierData>& Modifiers,
                                                         float DefaultSize,
                                                         float& OutMinWorldZ, float& OutMaxWorldZ)
{
	QUICK_SCOPE_CYCLE_COUNTER(Voxel_SurfaceGenerator_ComputeZBoundsFromModifiers);
	
    OutMinWorldZ = FLT_MAX;
    OutMaxWorldZ = -FLT_MAX;

    for (const FVoxelModifierData& Mod : Modifiers)
    {
        const FBoxSphereBounds Bounds = Mod.GetWorldBounds();
        OutMinWorldZ = FMath::Min(OutMinWorldZ, Bounds.Origin.Z - Bounds.SphereRadius);
        OutMaxWorldZ = FMath::Max(OutMaxWorldZ, Bounds.Origin.Z + Bounds.SphereRadius);
    }

    if (OutMinWorldZ == FLT_MAX)
    {
        OutMinWorldZ = 0.f;
        OutMaxWorldZ = DefaultSize;
    }
}

bool FVoxelSurfaceGenerator::IsGenerating() const
{
    for (const FGraphEventRef& E : PendingEvents)
    {
        if (E.IsValid() && !E->IsComplete()) return true;
    }
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Static helpers
// ──────────────────────────────────────────────────────────────────────────────
/*static*/ float FVoxelSurfaceGenerator::FindFloorZ(float WorldX, float WorldY,
                                                     float ZHigh, float ZLow,
                                                     const TArray<FVoxelModifierData>& Modifiers,
                                                     int32 MaxIterations)
{
    for (int32 i = 0; i < MaxIterations; ++i)
    {
        const float ZMid = (ZHigh + ZLow) * 0.5f;
        const float D    = FVoxelChunkGenerator::EvaluateDensity(FVector(WorldX, WorldY, ZMid), Modifiers);
        if (D > 0.f) ZLow  = ZMid;
        else         ZHigh = ZMid;
    }
    return (ZHigh + ZLow) * 0.5f;
}

/*static*/ FVector FVoxelSurfaceGenerator::EstimateNormal(float WorldX, float WorldY, float WorldZ,
                                                           const TArray<FVoxelModifierData>& Modifiers,
                                                           float StepEps)
{
    auto D = [&](float x, float y, float z)
    {
        return FVoxelChunkGenerator::EvaluateDensity(FVector(x, y, z), Modifiers);
    };
    FVector N(
        D(WorldX + StepEps, WorldY, WorldZ) - D(WorldX - StepEps, WorldY, WorldZ),
        D(WorldX, WorldY + StepEps, WorldZ) - D(WorldX, WorldY - StepEps, WorldZ),
        D(WorldX, WorldY, WorldZ + StepEps) - D(WorldX, WorldY, WorldZ - StepEps));
    return N.GetSafeNormal();
}

/*static*/ FVoxelSurfaceColumn FVoxelSurfaceGenerator::ScanColumn(
    float WorldX, float WorldY,
    float MinWorldZ, float MaxWorldZ,
    float StepSize, float SubFloorOffset,
    const TArray<FVoxelModifierData>& Modifiers)
{
	QUICK_SCOPE_CYCLE_COUNTER(Voxel_SurfaceGenerator_ScanColumn);
	
    FVoxelSurfaceColumn Col;

    const float JumpSize = SubFloorOffset > 0.f ? SubFloorOffset : StepSize;

    // Coarse pass: step down in JumpSize increments to locate transitions
    float PrevDensity = FVoxelChunkGenerator::EvaluateDensity(
        FVector(WorldX, WorldY, MaxWorldZ), Modifiers);
    float SearchZ = MaxWorldZ - JumpSize;

    while (SearchZ >= MinWorldZ)
    {
        const float CurrentDensity = FVoxelChunkGenerator::EvaluateDensity(
            FVector(WorldX, WorldY, SearchZ), Modifiers);

        // Transition: empty (≤0) → solid (>0) stepping downward = floor detected
        if (PrevDensity <= 0.f && CurrentDensity > 0.f)
        {
            // Fine pass: scan the coarse interval precisely using StepSize
            const float ScanTop = SearchZ + JumpSize;
            float FineZ         = ScanTop - StepSize;
            float FinePrev      = PrevDensity;

            while (FineZ >= SearchZ)
            {
                const float FineDensity = FVoxelChunkGenerator::EvaluateDensity(
                    FVector(WorldX, WorldY, FineZ), Modifiers);

                if (FinePrev <= 0.f && FineDensity > 0.f)
                {
                    const float FloorZ = FindFloorZ(WorldX, WorldY, FineZ + StepSize, FineZ, Modifiers);

                    FVoxelSurfaceLevel Level;
                    Level.WorldZ     = FloorZ;
                    Level.Normal     = EstimateNormal(WorldX, WorldY, FloorZ, Modifiers);
                    Level.LayerIndex = Col.Levels.Num();
                    Col.Levels.Add(Level);
                }

                FinePrev = FineDensity;
                FineZ   -= StepSize;
            }

            // Jump past the solid to search for cave floors below
            SearchZ    -= JumpSize;
            PrevDensity = FVoxelChunkGenerator::EvaluateDensity(
                FVector(WorldX, WorldY, SearchZ), Modifiers);
            SearchZ    -= JumpSize;
            continue;
        }

        PrevDensity = CurrentDensity;
        SearchZ    -= JumpSize;
    }

    return Col;
}
