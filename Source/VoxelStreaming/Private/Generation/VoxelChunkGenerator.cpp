// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Generation/VoxelChunkGenerator.h"

#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "Grid/VoxelGrid.h"
#include "Material/VoxelMaterialRegistry.h"
#include "Modifier/VoxelModifierTypes.h"
#include "System/VoxelRenderSubsystem.h"
#include "Voxel/VoxelTypes.h"

class FVoxelChunkTask
{
public:
    FVoxelChunkTask(FVoxelGrid& InGrid, FIntVector InCoord)
        : Grid(InGrid), ChunkCoord(InCoord) {}

    static TStatId GetStatId() { RETURN_QUICK_DECLARE_CYCLE_STAT(FVoxelChunkTask, STATGROUP_TaskGraphTasks); }
    static ENamedThreads::Type GetDesiredThread() { return ENamedThreads::AnyBackgroundThreadNormalTask; }
    static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

    void DoTask(ENamedThreads::Type, const FGraphEventRef&)
    {
        FVoxelChunkGenerator::BakeChunk(Grid, ChunkCoord);
    }

private:
    FVoxelGrid& Grid;
    FIntVector  ChunkCoord;
};

void FVoxelChunkGenerator::StartGeneration(const UWorld* World, FVoxelGrid& Grid, TArrayView<const FIntVector> DirtyChunks)
{
	ForceEndGeneration(World, Grid);
	GeneratingChunks = DirtyChunks;

    // Pre-create all chunks on the calling thread before going async to avoid TMap race conditions
    for (const FIntVector& Coord : GeneratingChunks)
    {
        Grid.GetOrCreateChunk(Coord);
    }

    PendingEvents.Reset();
    for (const FIntVector& Coord : GeneratingChunks)
    {
        FGraphEventRef Event = TGraphTask<FVoxelChunkTask>::CreateTask(nullptr)
            .ConstructAndDispatchWhenReady(Grid, Coord);
        PendingEvents.Add(Event);
    }
}

void FVoxelChunkGenerator::ForceEndGeneration(const UWorld* World, const FVoxelGrid& Grid)
{
	if (GeneratingChunks.IsEmpty())
	{
		return;
	}
	
    if (!PendingEvents.IsEmpty())
    {
        FTaskGraphInterface::Get().WaitUntilTasksComplete(PendingEvents);
        PendingEvents.Reset();
    }
	
	if (auto* RenderSys = UWorld::GetSubsystem<UVoxelRenderSubsystem>(World))
	{
		RenderSys->NotifyChunksBaked(GeneratingChunks, Grid);
	}
	
	GeneratingChunks.Reset();
}

bool FVoxelChunkGenerator::IsGenerating() const
{
	for (FGraphEventRef Event : PendingEvents)
	{
		if (!Event->IsComplete())
		{
			return true;
		}
	}
	return false;
}

float FVoxelChunkGenerator::SampleSDFTrilinear(const FVoxelModifierData& M, const FVector& LocalPos)
{
    // Points outside the baked bounds are definitively exterior. Return their
    // distance to the nearest point on the bounding box as a positive SDF value.
    if (!M.SDF.LocalBounds.IsInsideOrOn(LocalPos))
    {
        FVector Clamped(
            FMath::Clamp(LocalPos.X, M.SDF.LocalBounds.Min.X, M.SDF.LocalBounds.Max.X),
            FMath::Clamp(LocalPos.Y, M.SDF.LocalBounds.Min.Y, M.SDF.LocalBounds.Max.Y),
            FMath::Clamp(LocalPos.Z, M.SDF.LocalBounds.Min.Z, M.SDF.LocalBounds.Max.Z));
        return FVector::Dist(LocalPos, Clamped);
    }

    const int32 R = M.SDF.Resolution;
    FVector Extent = M.SDF.LocalBounds.GetExtent() * 2.f;
    FVector Rel = LocalPos - M.SDF.LocalBounds.Min;
    // Normalize to [0, R)
    float Fx = FMath::Clamp(Rel.X / Extent.X * R - 0.5f, 0.f, R - 1.001f);
    float Fy = FMath::Clamp(Rel.Y / Extent.Y * R - 0.5f, 0.f, R - 1.001f);
    float Fz = FMath::Clamp(Rel.Z / Extent.Z * R - 0.5f, 0.f, R - 1.001f);
    int32 X0 = (int32)Fx, Y0 = (int32)Fy, Z0 = (int32)Fz;
    int32 X1 = X0 + 1, Y1 = Y0 + 1, Z1 = Z0 + 1;
    float Tx = Fx - X0, Ty = Fy - Y0, Tz = Fz - Z0;
    auto S = [&](int32 X, int32 Y, int32 Z) { return M.SDF.Samples[X + Y * R + Z * R * R]; };
    float V000 = S(X0,Y0,Z0), V100 = S(X1,Y0,Z0), V010 = S(X0,Y1,Z0), V110 = S(X1,Y1,Z0);
    float V001 = S(X0,Y0,Z1), V101 = S(X1,Y0,Z1), V011 = S(X0,Y1,Z1), V111 = S(X1,Y1,Z1);
    float Vz0 = FMath::Lerp(FMath::Lerp(V000, V100, Tx), FMath::Lerp(V010, V110, Tx), Ty);
    float Vz1 = FMath::Lerp(FMath::Lerp(V001, V101, Tx), FMath::Lerp(V011, V111, Tx), Ty);
    return FMath::Lerp(Vz0, Vz1, Tz);
}

FVector2f FVoxelChunkGenerator::SampleUVTrilinear(const FVoxelModifierData& M, const FVector& LocalPos)
{
    if (M.SDF.UVSamples.IsEmpty() || M.SDF.Resolution <= 0) return FVector2f::ZeroVector;

    // Clamp outside-bounds positions to the boundary of the UV grid so outside voxels
    // get the nearest surface UV instead of zero (which would corrupt edge interpolation).
    const FVector Clamped(
        FMath::Clamp(LocalPos.X, M.SDF.LocalBounds.Min.X, M.SDF.LocalBounds.Max.X),
        FMath::Clamp(LocalPos.Y, M.SDF.LocalBounds.Min.Y, M.SDF.LocalBounds.Max.Y),
        FMath::Clamp(LocalPos.Z, M.SDF.LocalBounds.Min.Z, M.SDF.LocalBounds.Max.Z));

    const int32 R = M.SDF.Resolution;
    FVector Extent = M.SDF.LocalBounds.GetExtent() * 2.f;
    FVector Rel = Clamped - M.SDF.LocalBounds.Min;
    float Fx = FMath::Clamp(Rel.X / Extent.X * R - 0.5f, 0.f, R - 1.001f);
    float Fy = FMath::Clamp(Rel.Y / Extent.Y * R - 0.5f, 0.f, R - 1.001f);
    float Fz = FMath::Clamp(Rel.Z / Extent.Z * R - 0.5f, 0.f, R - 1.001f);
    int32 X0 = (int32)Fx, Y0 = (int32)Fy, Z0 = (int32)Fz;
    int32 X1 = X0 + 1, Y1 = Y0 + 1, Z1 = Z0 + 1;
    float Tx = Fx - X0, Ty = Fy - Y0, Tz = Fz - Z0;
    auto S = [&](int32 X, int32 Y, int32 Z) { return M.SDF.UVSamples[X + Y * R + Z * R * R]; };
    FVector2f V000 = S(X0,Y0,Z0), V100 = S(X1,Y0,Z0), V010 = S(X0,Y1,Z0), V110 = S(X1,Y1,Z0);
    FVector2f V001 = S(X0,Y0,Z1), V101 = S(X1,Y0,Z1), V011 = S(X0,Y1,Z1), V111 = S(X1,Y1,Z1);
    FVector2f Vz0 = FMath::Lerp(FMath::Lerp(V000, V100, Tx), FMath::Lerp(V010, V110, Tx), Ty);
    FVector2f Vz1 = FMath::Lerp(FMath::Lerp(V001, V101, Tx), FMath::Lerp(V011, V111, Tx), Ty);
    return FMath::Lerp(Vz0, Vz1, Tz);
}

static float EvaluateSingleModifierDensity(const FVoxelModifierData& M, const FVector& WorldPos)
{
	QUICK_SCOPE_CYCLE_COUNTER(Voxel_ChunkGenerator_EvaluateSingleModifierDensity);
	
    switch (M.Params.Type)
    {
    case EModifierType::PrimitiveSphere:
    {
        float Radius = M.Transform.GetScale3D().X;
        return Radius - FVector::Dist(WorldPos, M.Transform.GetLocation());
    }
    case EModifierType::PrimitiveBox:
    {
        FVector Local   = M.Transform.InverseTransformPosition(WorldPos);
        FVector HalfExt = M.Transform.GetScale3D();
        FVector Q       = FVector(FMath::Abs(Local.X), FMath::Abs(Local.Y), FMath::Abs(Local.Z)) - HalfExt;
        FVector QMax(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f), FMath::Max(Q.Z, 0.f));
        return -(QMax.Size() + FMath::Min(FMath::Max3(Q.X, Q.Y, Q.Z), 0.f));
    }
    case EModifierType::MeshSDF:
    {
        if (M.SDF.Resolution > 0 && M.SDF.Samples.Num() == M.SDF.Resolution * M.SDF.Resolution * M.SDF.Resolution)
        {
            FVector Local = M.Transform.InverseTransformPosition(WorldPos);
            return -FVoxelChunkGenerator::SampleSDFTrilinear(M, Local);
        }
        return -1.f;
    }
    default: return -1.f;
    }
}

struct FVoxelSurfaceData { FVector2f UV; uint16 MaterialHash; uint8 SurfaceType; };

/*static*/ float FVoxelChunkGenerator::EvaluateDensity(const FVector& WorldPos, const TArray<FVoxelModifierData>& Modifiers)
{
	QUICK_SCOPE_CYCLE_COUNTER(Voxel_SurfaceGenerator_EvaluateDensity);
	
    float Density = -1.f;
    for (const FVoxelModifierData& M : Modifiers)
    {
        // Skip modifiers whose bounds don't contain this position
        const FBoxSphereBounds Bounds = M.GetWorldBounds();
        if (!Bounds.GetBox().IsInsideOrOn(WorldPos))
            continue;

        const float D = EvaluateSingleModifierDensity(M, WorldPos);
        if (M.Params.Operation == EModifierOp::Add)
            Density = FMath::Max(Density, D);
        else
            Density = FMath::Min(Density, -D);
    }
    return Density;
}

// Single modifier pass: fills density and, when solid, surface data.
static void EvaluateVoxel(const FVector& WorldPos, const TArray<FVoxelModifierData>& Modifiers,
    float& OutDensity, uint8& OutSurfaceType, uint16& OutMaterialHash, FVector2f& OutUV)
{
    OutDensity     = -1.f;
    float BestAddD = -FLT_MAX;
    int32 BestAddIdx = INDEX_NONE;

    for (int32 i = 0; i < Modifiers.Num(); ++i)
    {
        const FVoxelModifierData& M = Modifiers[i];

        // Skip modifiers whose bounds don't contain this position
        const FBoxSphereBounds Bounds = M.GetWorldBounds();
        if (!Bounds.GetBox().IsInsideOrOn(WorldPos))
            continue;

        const float D = EvaluateSingleModifierDensity(M, WorldPos);
        if (M.Params.Operation == EModifierOp::Add)
        {
            if (D > OutDensity) OutDensity = D;
            if (D > BestAddD)  { BestAddD = D; BestAddIdx = i; }
        }
        else
        {
            OutDensity = FMath::Min(OutDensity, -D);
        }
    }

    if (OutDensity > 0.f && BestAddIdx != INDEX_NONE)
    {
        const FVoxelModifierData& Best = Modifiers[BestAddIdx];
        OutSurfaceType  = Best.Params.SurfaceType;
        OutMaterialHash = FVoxelMaterialRegistry::Register(Best.MaterialPath);
        const FVector Local = Best.Transform.InverseTransformPosition(WorldPos);
        OutUV = Best.SDF.UVSamples.IsEmpty()
            ? FVector2f::ZeroVector
            : FVoxelChunkGenerator::SampleUVTrilinear(Best, Local);
    }
    else
    {
        OutSurfaceType  = 0;
        OutMaterialHash = 0;
        OutUV           = FVector2f::ZeroVector;
    }
}

void FVoxelChunkGenerator::BakeChunk(FVoxelGrid& Grid, FIntVector ChunkCoord)
{
    FVoxelChunk* Chunk = Grid.FindChunk(ChunkCoord);
    if (!Chunk) return;

    TArray<FVoxelModifierData> Modifiers = Grid.GetModifiersForChunk(ChunkCoord);
    const int32 R  = Chunk->Resolution;
    const float VS = Chunk->VoxelSize;
    const FVector Origin = Chunk->ChunkOriginWorld();

    UE_LOG(LogTemp, Log, TEXT("BakeChunk [%d,%d,%d]: origin=%s  %d modifier(s)"),
        ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, *Origin.ToString(), Modifiers.Num());

    std::atomic<bool> bAnySolid{false};

    ParallelFor(R, [&](int32 Z)
    {
        bool bLocalSolid = false;
        for (int32 Y = 0; Y < R; ++Y)
        for (int32 X = 0; X < R; ++X)
        {
            const FVector WorldPos = Origin + FVector(X, Y, Z) * VS + FVector(VS * 0.5f);
            FVoxel& V = Chunk->At(X, Y, Z);
            V.Flags = 0;
            EvaluateVoxel(WorldPos, Modifiers, V.Density, V.SurfaceType, V.MaterialHash, V.UV);
            if (V.Density > 0.f) bLocalSolid = true;
        }
        if (bLocalSolid) bAnySolid.store(true, std::memory_order_relaxed);
    });

    Chunk->bHasSolidVoxels = bAnySolid.load();
    UE_LOG(LogTemp, Log, TEXT("BakeChunk [%d,%d,%d]: done — bHasSolidVoxels=%d  center density=%.3f"),
        ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, (int32)Chunk->bHasSolidVoxels,
        EvaluateDensity(Origin + FVector(R * VS * 0.5f), Modifiers));
    Grid.ClearDirtyFlag(ChunkCoord);
}
