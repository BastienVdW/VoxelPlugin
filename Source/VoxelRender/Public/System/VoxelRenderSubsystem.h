// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Types/VoxelRenderTypes.h"
#include "VoxelRenderSubsystem.generated.h"

class FVoxelGrid;

UCLASS()
class VOXELRENDER_API UVoxelRenderSubsystem
    : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override { return true; }

    // Called by URecallVoxelFlushProcessor after ForceEndGeneration.
    void NotifyChunksBaked(const TArrayView<const FIntVector>& BakedCoords, const FVoxelGrid& Grid);

    // Called when a chunk is evicted from streaming — removes from queue if pending.
    void NotifyChunkEvicted(FIntVector ChunkCoord);

private:
    void OnMeshReady(uint64 BatchVersion, TArray<FVoxelRenderMeshResult> Results);

    FCriticalSection PendingMutex;
    TMap<FIntVector, FVoxelRenderEvent> PendingEvents;
	
    TMap<uint64, TArray<FVoxelRenderMeshResult>> CompletedMeshBatches;
    uint64 NextBatchVersion = 0;
    uint64 NextBatchVersionToApply = 0;
    TWeakObjectPtr<class UVoxelChunkActorSubsystem> ChunkActorSubsystem;
};
