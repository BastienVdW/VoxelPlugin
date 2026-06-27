// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Grid/VoxelGrid.h"
#include "Generation/VoxelChunkGenerator.h"
#include "Streaming/VoxelView.h"
#include "VoxelStreamingSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnChunkEvicted, FIntVector /*ChunkCoord*/);

UCLASS()
class VOXELSTREAMING_API UVoxelStreamingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

    // Called explicitly by RecallVoxelStreamingProcessor — NOT auto-ticked.
    void Tick(float DeltaTime);

    // View management
    void RegisterView(uint32 ViewId, const FVoxelView& View);
    void UnregisterView(uint32 ViewId);
    void UpdateView(uint32 ViewId, const FVoxelView& View);

    // Generation — enqueues async bake for all dirty chunks, caches coords for GetLastBakedCoords
    void StartDirtyChunkGeneration();

    // Blocks until all enqueued bake tasks complete
    void ForceEndGeneration();
	
	bool IsGenerating() const { return Generator.IsValid() && Generator->IsGenerating(); }

    const FVoxelGrid&     GetGrid() const    { return *Grid; }
    FVoxelGrid&           GetMutableGrid()   { return *Grid; }
    FVoxelChunkGenerator& GetGenerator()     { return *Generator; }

    const TArray<FIntVector>& GetLastBakedCoords() const { return LastBakedCoords; }

	// Notified when a chunk is evicted so physics subsystem can destroy shapes
	FOnChunkEvicted OnChunkEvicted;

private:
	TWeakObjectPtr<class UVoxelRenderSubsystem> RenderSystem;

    TMap<uint32, FVoxelView>			Views;
    TUniquePtr<FVoxelGrid>				Grid;
    TUniquePtr<FVoxelChunkGenerator>	Generator;
	TSet<FIntVector>					DesiredChunks;

    TArray<FIntVector> GetAndClearDirtyChunks();

    TArray<FIntVector> LastBakedCoords;

    TSet<FIntVector> CurrentChunks;
    bool             bViewsDirty = true;
};
