// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "System/VoxelRenderSubsystem.h"

#include "Async/Async.h"
#include "Engine/World.h"
#include "Geometry/VoxelMarchingCubes.h"
#include "Geometry/VoxelGreedyMesh.h"
#include "Geometry/VoxelDualContouring.h"
#include "Grid/VoxelGrid.h"
#include "Settings/VoxelDeveloperSettings.h"
#include "System/VoxelChunkActorSubsystem.h"

namespace
{
struct FPendingMeshBatch
{
    explicit FPendingMeshBatch(const int32 NumResults)
        : TotalResults(NumResults)
    {
        Results.Reserve(NumResults);
    }

    FCriticalSection Mutex;
    int32 TotalResults;
    TArray<FVoxelRenderMeshResult> Results;
};
}

void UVoxelRenderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
	Collection.InitializeDependency<UVoxelChunkActorSubsystem>();
	
	ChunkActorSubsystem = UWorld::GetSubsystem<UVoxelChunkActorSubsystem>(GetWorld());
}

void UVoxelRenderSubsystem::Deinitialize()
{
    Super::Deinitialize();
	
	CompletedMeshBatches.Reset();
	ChunkActorSubsystem.Reset();
}

TStatId UVoxelRenderSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UVoxelRenderSubsystem, STATGROUP_Tickables);
}

void UVoxelRenderSubsystem::NotifyChunksBaked(
    const TArrayView<const FIntVector>& BakedCoords, const FVoxelGrid& Grid)
{
    TArray<TPair<FIntVector, FVoxelRenderEvent>> Prepared;
    Prepared.Reserve(BakedCoords.Num());

    for (const FIntVector& Coord : BakedCoords)
    {
        const FVoxelChunk* Chunk = Grid.QueryChunk(Coord);
        if (!Chunk || !Chunk->bHasSolidVoxels) continue;

        FVoxelRenderEvent Event;
        Event.ChunkCoord       = Coord;
        Event.ChunkOriginWorld = Chunk->ChunkOriginWorld();
        Event.Resolution       = Chunk->Resolution - 2 * Chunk->BorderSize; // canonical count
        Event.BorderSize       = Chunk->BorderSize;
        Event.VoxelSize        = Chunk->VoxelSize;
        Event.Voxels           = Chunk->Voxels; // full baked grid including border

        float Smoothness = 0.0f;
        for (const FVoxelModifierData& M : Grid.GetModifiersForChunk(Coord))
            if (M.Params.Operation == EModifierOp::Add)
                Smoothness = FMath::Max(Smoothness, M.Params.MeshSmoothing);
        Event.MeshSmoothing = Smoothness;

        Prepared.Emplace(Coord, MoveTemp(Event));
    }

    FScopeLock Lock(&PendingMutex);
    for (auto& [Coord, Event] : Prepared)
    {
        PendingEvents.Add(Coord, MoveTemp(Event));
    }
}

void UVoxelRenderSubsystem::NotifyChunkEvicted(FIntVector ChunkCoord)
{
    {
        FScopeLock Lock(&PendingMutex);
        PendingEvents.Remove(ChunkCoord);
    }

    if (UVoxelChunkActorSubsystem* ActorSys = ChunkActorSubsystem.Get())
    {
        ActorSys->ReleaseChunkActor(ChunkCoord);
    }
}

// Reconstructs an FVoxelChunk from a render event.
// The chunk already contains its full baked grid (canonical + border); no extra assembly needed.
static FVoxelChunk BuildChunkFromEvent(FVoxelRenderEvent& Evt)
{
    FVoxelChunk Chunk;
    Chunk.ChunkCoord = Evt.ChunkCoord;
    Chunk.VoxelSize  = Evt.VoxelSize;
    Chunk.BorderSize = Evt.BorderSize;
    Chunk.Resolution = Evt.Resolution + 2 * Evt.BorderSize;
    Chunk.Voxels     = MoveTemp(Evt.Voxels);
    return Chunk;
}

// Dispatches surface extraction to the correct geometry algorithm.
static FVoxelMeshData ExtractRawMesh(const FVoxelChunk& Chunk, EMeshAlgorithm Algorithm, float MeshSmoothing)
{
    if (Algorithm == EMeshAlgorithm::GreedyMesh)
        return FVoxelGreedyMesh::ExtractSurface(Chunk);
    if (Algorithm == EMeshAlgorithm::DualContouring)
        return FVoxelDualContouring::ExtractSurface(Chunk);
    return FVoxelMarchingCubes::ExtractSurface(Chunk, /*LODStep=*/1, MeshSmoothing);
}

// Groups flat triangle/vertex arrays by material hash into per-material mesh sections.
static void GroupMeshByMaterial(const FVoxelMeshData& RawMesh, FVoxelRenderMeshResult& OutResult)
{
    const int32 TriCount = RawMesh.Triangles.Num() / 3;
    for (int32 T = 0; T < TriCount; ++T)
    {
        const uint16 MatIdx = RawMesh.TriangleMaterialHashes.IsValidIndex(T)
            ? RawMesh.TriangleMaterialHashes[T] : 0;

        FVoxelRenderMeshSection& Section = OutResult.Sections.FindOrAdd(MatIdx);
        for (int32 V = 0; V < 3; ++V)
        {
            const int32 RawIdx = RawMesh.Triangles[T * 3 + V];
            Section.Vertices.Add(FVector(RawMesh.Vertices[RawIdx]));
            Section.Triangles.Add(Section.Vertices.Num() - 1);
            Section.UV0.Add(RawMesh.UVs.IsValidIndex(RawIdx)
                ? FVector2D(RawMesh.UVs[RawIdx]) : FVector2D::ZeroVector);
            // Flat normals — compute face normal once per triangle.
            if (V == 0)
            {
                const FVector A(RawMesh.Vertices[RawMesh.Triangles[T*3+0]]);
                const FVector B(RawMesh.Vertices[RawMesh.Triangles[T*3+1]]);
                const FVector C(RawMesh.Vertices[RawMesh.Triangles[T*3+2]]);
                const FVector N = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
                Section.Normals.Add(N);
                Section.Normals.Add(N);
                Section.Normals.Add(N);
            }
        }
    }
}

void UVoxelRenderSubsystem::Tick(float DeltaTime)
{
    TMap<FIntVector, FVoxelRenderEvent> EventsThisFrame;
    {
        FScopeLock Lock(&PendingMutex);
        if (PendingEvents.IsEmpty()) return;
        EventsThisFrame = MoveTemp(PendingEvents);
        PendingEvents.Reset();
    }

    const uint64 BatchVersion = NextBatchVersion++;
    const TSharedRef<FPendingMeshBatch, ESPMode::ThreadSafe> Batch =
        MakeShared<FPendingMeshBatch, ESPMode::ThreadSafe>(EventsThisFrame.Num());
	
    TWeakObjectPtr<UVoxelRenderSubsystem> WeakThis(this);
    for (auto& [Coord, Event] : EventsThisFrame)
    {
        FVoxelRenderEvent CapturedEvent = MoveTemp(Event);
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
            [WeakThis, Batch, BatchVersion, Evt = MoveTemp(CapturedEvent)]() mutable
        {
            const EMeshAlgorithm Algorithm = GetDefault<UVoxelDeveloperSettings>()->MeshAlgorithm;

            const FVoxelChunk  Chunk   = BuildChunkFromEvent(Evt);
            const FVoxelMeshData RawMesh = ExtractRawMesh(Chunk, Algorithm, Evt.MeshSmoothing);

            FVoxelRenderMeshResult Result;
            Result.ChunkCoord       = Evt.ChunkCoord;
            // ChunkOriginWorld already accounts for the border offset (computed in ChunkOriginWorld()).
            Result.ChunkOriginWorld = Evt.ChunkOriginWorld;
            GroupMeshByMaterial(RawMesh, Result);

            TArray<FVoxelRenderMeshResult> CompletedResults;
            {
                FScopeLock Lock(&Batch->Mutex);
                const int32 NewIndex = Batch->Results.Add(MoveTemp(Result));
                if (Batch->TotalResults == NewIndex + 1)
                {
                    CompletedResults = MoveTemp(Batch->Results);
                }
            }

            if (CompletedResults.IsEmpty())
            {
            	return;
            }
            		
            AsyncTask(ENamedThreads::GameThread,
				[WeakThis, BatchVersion, Results = MoveTemp(CompletedResults)]() mutable
			{
				if (UVoxelRenderSubsystem* S = WeakThis.Get())
				{
					S->OnMeshReady(BatchVersion, MoveTemp(Results));
				}
			});
        });
    }
}

void UVoxelRenderSubsystem::OnMeshReady(
    const uint64 BatchVersion, TArray<FVoxelRenderMeshResult> Results)
{
    check(IsInGameThread());

    CompletedMeshBatches.Add(BatchVersion, MoveTemp(Results));
	if (BatchVersion > NextBatchVersionToApply)
	{
		return;
	}

    UVoxelChunkActorSubsystem* ActorSys = ChunkActorSubsystem.Get();
    while (const TArray<FVoxelRenderMeshResult>* ReadyResults = CompletedMeshBatches.Find(NextBatchVersionToApply))
    {
        if (ActorSys)
        {
            for (const FVoxelRenderMeshResult& Result : *ReadyResults)
            {
                ActorSys->ApplyMesh(Result);
            }
        }

        CompletedMeshBatches.Remove(NextBatchVersionToApply);
        ++NextBatchVersionToApply;
    }
}
