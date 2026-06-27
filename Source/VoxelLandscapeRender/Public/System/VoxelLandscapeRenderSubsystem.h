// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Generation/LandscapeMeshGenerator.h"
#include "Subsystems/WorldSubsystem.h"
#include "VoxelLandscapeTypes.h"
#include "VoxelSurfaceTypes.h"
#include "VoxelLandscapeRenderSubsystem.generated.h"

class UVoxelLandscapeSubsystem;
class UVoxelSurfaceSubsystem;
class UVoxelLandscapeMeshSubsystem;
class UMaterialInterface;

// Snapshot of one chunk's data passed to background mesh generation tasks.
struct FLandscapeRenderEvent
{
    FIntVector           MeshKey;   // (ChunkCoord.X, ChunkCoord.Y, SurfaceLayerIndex)
    int32                Revision = 0;
    FVoxelLandscapeChunk ChunkData;
    FVoxelSurfaceChunk   SurfaceData;
    float                VoxelSize;
};

// Aggregated result for one chunk — delivered to game thread.
struct FLandscapeRenderResult
{
    FIntVector                          MeshKey;
    int32                               Revision  = 0;
    float                               VoxelSize = 0.f;
    TMap<FName, FLandscapeLayerSection> Layers;
};

UCLASS()
class VOXELLANDSCAPERENDER_API UVoxelLandscapeRenderSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override { return true; }

    // Called by the OnSimFlushed delegate (game thread) after ForceEndSimulation.
    void NotifyLandscapeUpdated(const TArray<FIntVector2>& DirtyChunks);

private:
    void OnMeshReady(FLandscapeRenderResult Result);

    FCriticalSection PendingMutex;
    TMap<FIntVector, FLandscapeRenderEvent> PendingEvents;

    TMap<FIntVector, int32> MeshRevisions;

    TWeakObjectPtr<UVoxelLandscapeSubsystem> LandscapeSystem;
    TWeakObjectPtr<UVoxelSurfaceSubsystem>   SurfaceSystem;
    TWeakObjectPtr<UVoxelLandscapeMeshSubsystem>  ProcMeshSubsystem;
};
