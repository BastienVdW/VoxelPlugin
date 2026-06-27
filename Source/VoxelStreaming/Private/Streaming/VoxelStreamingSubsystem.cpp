// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Streaming/VoxelStreamingSubsystem.h"

#include "Engine/World.h"
#include "Settings/VoxelDeveloperSettings.h"
#include "Subsystems/SubsystemCollection.h"
#include "System/VoxelRenderSubsystem.h"

void UVoxelStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UVoxelRenderSubsystem>();

	RenderSystem = UWorld::GetSubsystem<UVoxelRenderSubsystem>(GetWorld());

	const UVoxelDeveloperSettings* S = GetDefault<UVoxelDeveloperSettings>();
	FVoxelGridConfig Cfg;
	Cfg.DefaultResolution = S->ChunkResolution;
	Cfg.DefaultVoxelSize  = S->VoxelSize;
	Cfg.DefaultBorderSize = S->ChunkBorderSize;
	Grid      = MakeUnique<FVoxelGrid>(Cfg);
	Generator = MakeUnique<FVoxelChunkGenerator>();
}

void UVoxelStreamingSubsystem::Deinitialize()
{
	Generator.Reset();
	Grid.Reset();
	RenderSystem.Reset();
	Super::Deinitialize();
}

TArray<FIntVector> UVoxelStreamingSubsystem::GetAndClearDirtyChunks()
{
	if (!Grid)
	{
		return {};
	}
	TArray<FIntVector> Dirty = Grid->GetDirtyChunks();
	for (const FIntVector& C : Dirty)
	{
		Grid->ClearDirtyFlag(C);
	}
	return Dirty;
}

void UVoxelStreamingSubsystem::StartDirtyChunkGeneration()
{
	checkf(!IsGenerating(), TEXT("Already generating"));
	
	const TArray<FIntVector> Dirty = GetAndClearDirtyChunks();
	LastBakedCoords = Dirty;
	if (Dirty.IsEmpty())
	{
		return;
	}
	
	if (Generator)
	{
		Grid->SetGenerating(true);
		Generator->StartGeneration(GetWorld(), *Grid, Dirty);
	}
}

void UVoxelStreamingSubsystem::ForceEndGeneration()
{
	if (Generator && Grid)
	{
		Generator->ForceEndGeneration(GetWorld(), *Grid);
		Grid->SetGenerating(false);
	}
}

void UVoxelStreamingSubsystem::RegisterView(uint32 ViewId, const FVoxelView& View)
{
    Views.Add(ViewId, View);
    bViewsDirty = true;
}

void UVoxelStreamingSubsystem::UnregisterView(uint32 ViewId)
{
    Views.Remove(ViewId);
    bViewsDirty = true;
}

void UVoxelStreamingSubsystem::UpdateView(uint32 ViewId, const FVoxelView& View)
{
    if (FVoxelView* Existing = Views.Find(ViewId))
    {
        if (!Existing->Position.Equals(View.Position, 1.f) || Existing->FarRadius != View.FarRadius)
        {
            bViewsDirty = true;
        }
        *Existing = View;
    }
}

void UVoxelStreamingSubsystem::Tick(float DeltaTime)
{
    if (!Grid || !bViewsDirty) return;
	
	QUICK_SCOPE_CYCLE_COUNTER(Voxel_Streaming_Tick);

    bViewsDirty = false;

    // Collect only chunks that both overlap a view sphere AND have at least one modifier.
    // Iterates modifiers rather than all chunk coords in range — O(modifiers_in_view) instead of O(chunks_in_sphere).
    DesiredChunks.Reset();
    for (const auto& [Id, View] : Views)
    {
		QUICK_SCOPE_CYCLE_COUNTER(Voxel_Streaming_CollectChunks);
        DesiredChunks.Append(Grid->GetChunksWithModifiersInSphere(View.Position, View.FarRadius));
    }

    // Evict chunks that dropped out of the desired set
    for (const FIntVector& Coord : CurrentChunks)
    {
		QUICK_SCOPE_CYCLE_COUNTER(Voxel_Streaming_EvictChunk);

        if (!DesiredChunks.Contains(Coord))
        {
        	TWeakObjectPtr<UVoxelRenderSubsystem> WeakRenderSystem = RenderSystem;
        	AsyncTask(ENamedThreads::GameThread, [Coord, WeakRenderSystem]()
        	{
        		if (WeakRenderSystem.IsValid())
        		{
					WeakRenderSystem->NotifyChunkEvicted(Coord);
				}
        	});

            OnChunkEvicted.Broadcast(Coord);
            Grid->EvictChunk(Coord);
        }
    }

    // Create chunks that are newly desired (modifier check already done by GetChunksWithModifiersInSphere)
    for (const FIntVector& Coord : DesiredChunks)
    {
		QUICK_SCOPE_CYCLE_COUNTER(Voxel_Streaming_CreateChunk);

        if (!CurrentChunks.Contains(Coord))
        {
            Grid->GetOrCreateChunk(Coord);
        }
    }

    CurrentChunks = MoveTemp(DesiredChunks);
}
