// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "System/VoxelLandscapeRenderSubsystem.h"

#include "Async/Async.h"
#include "Engine/World.h"
#include "Generation/LandscapeMeshGenerator.h"
#include "Materials/MaterialInterface.h"
#include "Settings/VoxelDeveloperSettings.h"
#include "System/VoxelLandscapeSubsystem.h"
#include "System/VoxelLandscapeMeshSubsystem.h"
#include "System/VoxelSurfaceSubsystem.h"

void UVoxelLandscapeRenderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    Collection.InitializeDependency<UVoxelLandscapeSubsystem>();
    LandscapeSystem = UWorld::GetSubsystem<UVoxelLandscapeSubsystem>(GetWorld());

    Collection.InitializeDependency<UVoxelSurfaceSubsystem>();
    SurfaceSystem = UWorld::GetSubsystem<UVoxelSurfaceSubsystem>(GetWorld());

    Collection.InitializeDependency<UVoxelLandscapeMeshSubsystem>();
    ProcMeshSubsystem = UWorld::GetSubsystem<UVoxelLandscapeMeshSubsystem>(GetWorld());

    if (LandscapeSystem.IsValid())
    {
        LandscapeSystem->OnSimFlushed.AddUObject(this, &UVoxelLandscapeRenderSubsystem::NotifyLandscapeUpdated);
    }
}

void UVoxelLandscapeRenderSubsystem::Deinitialize()
{
    Super::Deinitialize();
    MeshRevisions.Reset();
}

TStatId UVoxelLandscapeRenderSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UVoxelLandscapeRenderSubsystem, STATGROUP_Tickables);
}

void UVoxelLandscapeRenderSubsystem::NotifyLandscapeUpdated(const TArray<FIntVector2>& DirtyChunks)
{
    // Called on game thread after ForceEndSimulation — safe to read subsystem state.
    if (!LandscapeSystem.IsValid() || !SurfaceSystem.IsValid())
    {
	    return;
    }

	const UVoxelDeveloperSettings* VoxelSettings = GetDefault<UVoxelDeveloperSettings>();
    const float VoxelSize = VoxelSettings->VoxelSize;
    const int32 ColRes = VoxelSettings->ChunkResolution + 2;
    const int32 ExpectedCells = ColRes * ColRes;
    const TMap<FLandscapeChunkKey, FVoxelLandscapeChunk>& AllChunks = LandscapeSystem->GetAllChunks();
    const TMap<FIntVector2, FVoxelSurfaceChunk>& AllSurfaceChunks = SurfaceSystem->GetAllSurfaceChunks();

    // Build coord -> layer index list
    TMap<FIntVector2, TArray<int32>> CoordToLayers;
    for (const auto& [Key, Chunk] : AllChunks)
    {
        CoordToLayers.FindOrAdd(Key.ChunkCoord).AddUnique(Key.SurfaceLayerIndex);
    }

    TMap<FIntVector, FLandscapeRenderEvent> Prepared;

    for (const FIntVector2& Coord : DirtyChunks)
    {
        const TArray<int32>* LayerIndices = CoordToLayers.Find(Coord);
        if (!LayerIndices) continue;

        for (int32 SurfaceLayerIndex : *LayerIndices)
        {
            const FVoxelLandscapeChunk* Chunk =
                AllChunks.Find(FLandscapeChunkKey{Coord, SurfaceLayerIndex});
            if (!Chunk) continue;

            const FVoxelSurfaceChunk* SurfChunk = AllSurfaceChunks.Find(Coord);
            if (!SurfChunk || SurfChunk->bDirty || SurfChunk->Columns.Num() != ExpectedCells)
            {
                continue;
            }

            const FIntVector MeshKey(Coord.X, Coord.Y, SurfaceLayerIndex);

            FLandscapeRenderEvent Event;
            Event.MeshKey     = MeshKey;
            Event.Revision    = MeshRevisions.FindOrAdd(MeshKey) + 1;
            Event.ChunkData   = *Chunk;
            Event.VoxelSize   = VoxelSize;
            Event.SurfaceData = *SurfChunk;

            Prepared.Add(MeshKey, MoveTemp(Event));
        }
    }

    FScopeLock Lock(&PendingMutex);
    for (auto& [Key, Event] : Prepared)
    {
        MeshRevisions.FindOrAdd(Key) = Event.Revision;
    	PendingEvents.Add(Key, MoveTemp(Event));
    }
}

void UVoxelLandscapeRenderSubsystem::Tick(float DeltaTime)
{
    TMap<FIntVector, FLandscapeRenderEvent> EventsThisFrame;
    {
        FScopeLock Lock(&PendingMutex);
        if (PendingEvents.IsEmpty()) return;
        EventsThisFrame = MoveTemp(PendingEvents);
        PendingEvents.Reset();
    }

    TWeakObjectPtr<UVoxelLandscapeRenderSubsystem> WeakThis(this);

    for (auto& [MeshKey, Event] : EventsThisFrame)
    {
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
            [WeakThis, Evt = MoveTemp(Event)]() mutable
        {
            FLandscapeRenderResult Result;
            Result.MeshKey   = Evt.MeshKey;
            Result.Revision  = Evt.Revision;
            Result.VoxelSize = Evt.VoxelSize;

            const FVector2D ActorOrigin = UVoxelLandscapeMeshSubsystem::ComputeActorOrigin(Evt.MeshKey, Evt.VoxelSize);

            for (const auto& [LayerName, Layer] : Evt.ChunkData.Layers)
            {
                Result.Layers.Add(LayerName, FLandscapeMeshGenerator::ComputeSection(
                    Evt.ChunkData, Evt.SurfaceData, LayerName, Evt.VoxelSize, ActorOrigin));
            }

            AsyncTask(ENamedThreads::GameThread,
                [WeakThis, MeshResult = MoveTemp(Result)]() mutable
            {
                if (WeakThis.IsValid())
                {
                    WeakThis->OnMeshReady(MoveTemp(MeshResult));
                }
            });
        });
    }
}

void UVoxelLandscapeRenderSubsystem::OnMeshReady(FLandscapeRenderResult Result)
{
    check(IsInGameThread());

    const int32* CurrentRevision = MeshRevisions.Find(Result.MeshKey);
    if (!CurrentRevision || *CurrentRevision != Result.Revision)
    {
        return;
    }

    if (!ProcMeshSubsystem.IsValid()) return;

    const UVoxelDeveloperSettings* Settings = GetDefault<UVoxelDeveloperSettings>();

    for (auto& [LayerName, Section] : Result.Layers)
    {
        const FLandscapeLayerConfig* LayerConfig = Settings->GetLandscapeLayerConfig(LayerName);
        UMaterialInterface* Material = LayerConfig ? LayerConfig->Material.LoadSynchronous() : nullptr;

        ProcMeshSubsystem->UpdateChunkSection(Result.MeshKey, LayerName, Material,
                                              MoveTemp(Section), Result.VoxelSize);
    }
}
