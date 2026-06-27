// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "System/VoxelLandscapeSubsystem.h"

#include "Engine/World.h"
#include "Simulation/LandscapeSimTask.h"
#include "Settings/VoxelDeveloperSettings.h"
#include "System/VoxelSurfaceSubsystem.h"
#include "Utils/VoxelLandscapeUtils.h"
#include "Utility/Math/RecallMathUtils.h"

void UVoxelLandscapeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
	
	Collection.InitializeDependency<UVoxelSurfaceSubsystem>();
    SurfaceSubsystem = UWorld::GetSubsystem<UVoxelSurfaceSubsystem>(GetWorld());
    if (SurfaceSubsystem.IsValid())
    {
        SurfaceSubsystem->OnSurfaceFlushed.AddUObject(this, &UVoxelLandscapeSubsystem::NotifySurfaceUpdated);
    }
}

void UVoxelLandscapeSubsystem::Deinitialize()
{
    if (SurfaceSubsystem.IsValid())
    {
        SurfaceSubsystem->OnSurfaceFlushed.RemoveAll(this);
    }
	Super::Deinitialize();
}

void UVoxelLandscapeSubsystem::StartSimulation()
{
	checkf(!IsSimulating(), TEXT("StartSimulation called while simulation is in-flight"));
	
	if (!SurfaceSubsystem.IsValid())
	{
		return;
	}
	
	bIsSimulating = true;
	
    const auto* Settings = GetDefault<UVoxelDeveloperSettings>();
    const int32 ColRes = VoxelLandscape::Utils::GetColumnResolution();

	const TArray<FLandscapeChunkKey> AwakeKeys = LandscapeGrid.GetAwakeKeys();
	LastDirtyChunks.Reserve(AwakeKeys.Num());
	LastDirtyChunkKeys.Reserve(AwakeKeys.Num());

    // Count outputs first so PendingOutputs can be allocated in one shot.
    // AddDefaulted() inside the dispatch loop would reallocate the array mid-flight,
    // invalidating Output pointers already stored in dispatched tasks, causing heap corruption.
    int32 TotalOutputs = 0;
    for (const FLandscapeChunkKey& Key : AwakeKeys)
    {
		const FVoxelLandscapeChunk* Chunk = LandscapeGrid.Find(Key);
		if (Chunk)
		{
			for (const auto& [LayerName, Layer] : Chunk->Layers)
			{
	            const FLandscapeLayerConfig* const LayerConfig = Settings->GetLandscapeLayerConfig(LayerName);
				if (LayerConfig) ++TotalOutputs;
			}
		}
    }

    PendingOutputs.SetNum(TotalOutputs); // stable allocation - no realloc below
    int32 OutIdx = 0;
	TSet<FIntVector2> LastDirtyChunkSet;

    for (const FLandscapeChunkKey& Key : AwakeKeys)
    {
		FVoxelLandscapeChunk* Chunk = LandscapeGrid.Find(Key);
        if (!Chunk) continue;
		LastDirtyChunkKeys.Add(Key);
		if (!LastDirtyChunkSet.Contains(Key.ChunkCoord))
		{
			LastDirtyChunkSet.Add(Key.ChunkCoord);
			LastDirtyChunks.Add(Key.ChunkCoord);
		}

        const FVoxelSurfaceChunk* SurfChunk = SurfaceSubsystem->GetSurfaceChunk(Key.ChunkCoord);

        for (auto& [LayerName, Layer] : Chunk->Layers)
        {
            const FLandscapeLayerConfig* LayerConfig = Settings->GetLandscapeLayerConfig(LayerName);
            if (!LayerConfig) continue;

            // Build SurfaceZ array from snapshot. Cells whose target surface layer
            // no longer exists are discarded instead of falling to an artificial Z=0.
            TArray<float> SurfZ;
            SurfZ.SetNumZeroed(ColRes * ColRes);
            TArray<FLandscapeCell> SimCells = Layer.Cells;
            if (SurfChunk && SurfChunk->Columns.Num() == ColRes * ColRes)
            {
                for (int32 i = 0; i < ColRes * ColRes; ++i)
                {
                    const auto& Col = SurfChunk->Columns[i];
                    if (Col.Levels.IsValidIndex(Key.SurfaceLayerIndex))
                    {
                        SurfZ[i] = Col.Levels[Key.SurfaceLayerIndex].WorldZ;
                    }
                    else if (SimCells.IsValidIndex(i))
                    {
                        SimCells[i].Depth = 0.f;
                    }
                }
            }
            else
            {
                for (FLandscapeCell& Cell : SimCells)
                {
                    Cell.Depth = 0.f;
                }
            }

            FLandscapeSimInput Input;
            Input.Cells             = MoveTemp(SimCells);
            Input.SurfaceZ          = MoveTemp(SurfZ);
            Input.Params            = LayerConfig->FluidParams;
            Input.GridSize          = ColRes;
            Input.bUseGhostBorder   = true;
            Input.LayerName         = LayerName;
            Input.ChunkCoord        = Key.ChunkCoord;
            Input.SurfaceLayerIndex = Key.SurfaceLayerIndex;

            // Pre-populate border cells from neighbors so fluid can flow across chunk seams.
            CopyBorderFromNeighbors(LayerName, Key.ChunkCoord, Key.SurfaceLayerIndex, Input.Cells);

            FLandscapeSimOutput* Output = &PendingOutputs[OutIdx++];

            FGraphEventRef Event = TGraphTask<FLandscapeSimTask>::CreateTask(nullptr)
                .ConstructAndDispatchWhenReady(MoveTemp(Input), Output);
            PendingEvents.Add(Event);
        }
    }
}

void UVoxelLandscapeSubsystem::CopyBorderFromNeighbors(FName LayerName, FIntVector2 ChunkCoord,
                                                        int32 SurfLayerIdx,
                                                        TArray<FLandscapeCell>& InOutCells) const
{
    const int32 ColRes = VoxelLandscape::Utils::GetColumnResolution();
    if (!VoxelLandscape::Utils::IsValidCellBuffer(InOutCells, ColRes)) return;

    // Seed ghost borders from neighbor's inner cells (not neighbor's ghost borders) so that
    // the simulation uses actual fluid values at the seam, matching mesh vertex reads.
	VoxelLandscape::Utils::FBorderDirection Dirs[4];
	VoxelLandscape::Utils::GetBorderDirections(ColRes, Dirs);

    for (const VoxelLandscape::Utils::FBorderDirection& Dir : Dirs)
    {
        const FLandscapeChunkKey NeighborKey{ChunkCoord + Dir.Delta, SurfLayerIdx};
        const FVoxelLandscapeChunk* Neighbor = LandscapeGrid.Find(NeighborKey);
        if (!Neighbor) continue;

        const FVoxelLandscapeLayer* NLayer = Neighbor->Layers.Find(LayerName);
        if (!NLayer || !VoxelLandscape::Utils::IsValidCellBuffer(NLayer->Cells, ColRes)) continue;

		for (int32 Line = 0; Line < ColRes; ++Line)
		{
			InOutCells[VoxelLandscape::Utils::GetBorderCellIndex(Dir, Line, ColRes)] =
				NLayer->Cells[VoxelLandscape::Utils::GetNeighborBorderCellIndex(Dir, Line, ColRes)];
		}
    }
}

void UVoxelLandscapeSubsystem::ForceEndSimulation()
{
    WaitForPendingSimulation();

    CommitBorderTransfers();

	UpdateSimulationActivity();

    CommitPendingOutputs();

    SpillToLowerLayers();

    MergeBorderCells(LastDirtyChunks);

	const TArray<FIntVector2> AllDirtyCoords = LandscapeGrid.GetDirtyChunkCoords();
	
	LandscapeGrid.ClearDirty();
	
	LastDirtyChunks.Reset();
	LastDirtyChunkKeys.Reset();
	PendingEvents.Reset();
	PendingOutputs.Reset();
	
	bIsSimulating = false;

	if (!AllDirtyCoords.IsEmpty())
	{
		OnSimFlushed.Broadcast(AllDirtyCoords);
	}
}

void UVoxelLandscapeSubsystem::WaitForPendingSimulation()
{
	if (!PendingEvents.IsEmpty())
	{
		FTaskGraphInterface::Get().WaitUntilTasksComplete(PendingEvents);
		PendingEvents.Reset();
	}
}

void UVoxelLandscapeSubsystem::CommitPendingOutputs()
{
	for (const FLandscapeSimOutput& Output : PendingOutputs)
	{
		const FLandscapeChunkKey Key{Output.ChunkCoord, Output.SurfaceLayerIndex};
		if (FVoxelLandscapeChunk* Chunk = LandscapeGrid.Find(Key))
		{
			Chunk->Layers.FindOrAdd(Output.LayerName).Cells = Output.Cells;
		}
	}
	PendingOutputs.Reset();
}

void UVoxelLandscapeSubsystem::UpdateSimulationActivity()
{
	const UVoxelDeveloperSettings* Settings = GetDefault<UVoxelDeveloperSettings>();
	const int32 ColRes = VoxelLandscape::Utils::GetColumnResolution();
	TMap<FLandscapeChunkKey, bool> UnsettledByKey;
	TMap<FLandscapeChunkKey, bool> ChangedByKey;

	for (const FLandscapeSimOutput& Output : PendingOutputs)
	{
		const FLandscapeChunkKey Key{Output.ChunkCoord, Output.SurfaceLayerIndex};
		const FVoxelLandscapeChunk* Chunk = LandscapeGrid.Find(Key);
		const FVoxelLandscapeLayer* OldLayer = Chunk ? Chunk->Layers.Find(Output.LayerName) : nullptr;
		const FLandscapeLayerConfig* Config = Settings->GetLandscapeLayerConfig(Output.LayerName);
		if (!OldLayer || !Config || OldLayer->Cells.Num() != Output.Cells.Num())
		{
			UnsettledByKey.FindOrAdd(Key) = true;
			ChangedByKey.FindOrAdd(Key) = true;
			continue;
		}

		const float VelocityThreshold = Recall::Math::Utils::UnitsPerSecondToPerFrame(Config->FluidParams.SleepVelocityThreshold);
		for (int32 Y = 1; Y < ColRes - 1; ++Y)
		{
			for (int32 X = 1; X < ColRes - 1; ++X)
			{
				const int32 Index = X + Y * ColRes;
				const FLandscapeCell& OldCell = OldLayer->Cells[Index];
				const FLandscapeCell& NewCell = Output.Cells[Index];
				if (FMath::Abs(NewCell.Depth - OldCell.Depth) > Config->FluidParams.SleepDepthThreshold)
				{
					ChangedByKey.FindOrAdd(Key) = true;
					UnsettledByKey.FindOrAdd(Key) = true;
				}
				if (NewCell.Velocity.Size() > VelocityThreshold)
				{
					UnsettledByKey.FindOrAdd(Key) = true;
				}
			}
		}
	}

	for (const FLandscapeChunkKey& Key : LastDirtyChunkKeys)
	{
		FVoxelLandscapeChunk* Chunk = LandscapeGrid.Find(Key);
		if (!Chunk) continue;
		if (ChangedByKey.FindRef(Key))
		{
			LandscapeGrid.MarkDirty(Key);
		}
		if (UnsettledByKey.FindRef(Key))
		{
			Chunk->StableSimulationSteps = 0;
			LandscapeGrid.Wake(Key);
			continue;
		}

		++Chunk->StableSimulationSteps;
		int32 RequiredStableSteps = 1;
		for (const auto& [LayerName, Layer] : Chunk->Layers)
		{
			if (const FLandscapeLayerConfig* Config = Settings->GetLandscapeLayerConfig(LayerName))
			{
				RequiredStableSteps = FMath::Max(RequiredStableSteps, Config->FluidParams.SleepAfterStableSteps);
			}
		}
		if (Chunk->StableSimulationSteps >= RequiredStableSteps)
		{
			LandscapeGrid.Sleep(Key);
		}
	}
}

void UVoxelLandscapeSubsystem::CommitBorderTransfers()
{
    const int32 ColRes = VoxelLandscape::Utils::GetColumnResolution();
    const int32 CellCount = VoxelLandscape::Utils::GetCellCount(ColRes);

	VoxelLandscape::Utils::FBorderDirection Dirs[4];
	VoxelLandscape::Utils::GetBorderDirections(ColRes, Dirs);

	TMap<FLandscapePendingOutputKey, FLandscapeSimOutput*> PendingOutputByKey;
	PendingOutputByKey.Reserve(PendingOutputs.Num());
	for (FLandscapeSimOutput& Output : PendingOutputs)
	{
		PendingOutputByKey.Add({ Output.ChunkCoord, Output.SurfaceLayerIndex, Output.LayerName }, &Output);
	}

    for (FLandscapeSimOutput& Output : PendingOutputs)
    {
        if (Output.Cells.Num() != CellCount) continue;

        for (const VoxelLandscape::Utils::FBorderDirection& Dir : Dirs)
        {
            const FLandscapeChunkKey NeighborKey{Output.ChunkCoord + Dir.Delta, Output.SurfaceLayerIndex};

            for (int32 Line = 0; Line < ColRes; ++Line)
            {
                const int32 GhostIdx = VoxelLandscape::Utils::GetBorderCellIndex(Dir, Line, ColRes);
                const int32 NeighborInnerIdx = VoxelLandscape::Utils::GetNeighborBorderCellIndex(Dir, Line, ColRes);

                const float BaselineDepth = LandscapeGrid.GetCellDepth(NeighborKey, Output.LayerName, NeighborInnerIdx);
                const float TransferDepth = Output.Cells[GhostIdx].Depth - BaselineDepth;
                const FVector2f TransferVelocity = Output.Cells[GhostIdx].Velocity;
                Output.Cells[GhostIdx].Depth = 0.f;
                Output.Cells[GhostIdx].Velocity = FVector2f::ZeroVector;

                if (TransferDepth <= 0.f) continue;

                AddDepthToLayer(PendingOutputByKey, NeighborKey, Output.LayerName, NeighborInnerIdx, TransferDepth, TransferVelocity, CellCount);
            }
        }
    }
}

void UVoxelLandscapeSubsystem::AddDepthToLayer(
	const TMap<FLandscapePendingOutputKey, FLandscapeSimOutput*>& PendingOutputByKey,
	const FLandscapeChunkKey& Key,
	const FName LayerName,
	const int32 Index,
	const float Depth,
	const FVector2f Velocity,
	const int32 CellCount)
{
	if (Depth <= 0.f) return;

	if (FLandscapeSimOutput* const* Output = PendingOutputByKey.Find({ Key.ChunkCoord, Key.SurfaceLayerIndex, LayerName }))
	{
		if ((*Output)->Cells.Num() == CellCount && (*Output)->Cells.IsValidIndex(Index))
		{
			FLandscapeCell& Cell = (*Output)->Cells[Index];
			const float ExistingDepth = Cell.Depth;
			Cell.Depth += Depth;
			if (Cell.Depth > UE_SMALL_NUMBER)
			{
				Cell.Velocity = (Cell.Velocity * ExistingDepth + Velocity * Depth) / Cell.Depth;
			}
		}
		return;
	}

	LandscapeGrid.AddDepthToLayer(Key, LayerName, Index, Depth, Velocity, CellCount);
}

const FVoxelLandscapeChunk* UVoxelLandscapeSubsystem::GetChunk(FIntVector2 ChunkCoord,
                                                                 int32 SurfaceLayerIndex) const
{
    return LandscapeGrid.Find(FLandscapeChunkKey{ChunkCoord, SurfaceLayerIndex});
}

void UVoxelLandscapeSubsystem::SetCellValue(FName LayerName, FIntVector2 ChunkCoord,
                                             int32 SurfaceLayerIndex,
                                             FIntVector2 ColumnCoord, float Value)
{
    const int32 ColRes = VoxelLandscape::Utils::GetColumnResolution();
    FLandscapeChunkKey Key{ChunkCoord, SurfaceLayerIndex};
    if (FVoxelLandscapeChunk* Chunk = LandscapeGrid.Find(Key))
    {
        FVoxelLandscapeLayer* Layer = Chunk->Layers.Find(LayerName);
        const int32 Idx = ColumnCoord.X + ColumnCoord.Y * ColRes;
        if (Layer && Layer->Cells.IsValidIndex(Idx))
        {
            Layer->Cells[Idx].Depth = Value;
            LandscapeGrid.MarkDirtyAndWake(Key);
        }
    }
}

void UVoxelLandscapeSubsystem::ApplyBrush(FName LayerName, FVector WorldPos, float Radius, float Value, int32 SurfaceLayerIndex)
{
    checkf(!IsSimulating(), TEXT("ApplyBrush called while simulation is in-flight"));

    if (!SurfaceSubsystem.IsValid())
    {
	    return;
    }

    const FBox2D BrushBounds(
        FVector2D(WorldPos.X - Radius, WorldPos.Y - Radius),
        FVector2D(WorldPos.X + Radius, WorldPos.Y + Radius));
	
    const TArray<FIntVector2> SurfaceChunkCoords =
        SurfaceSubsystem->GetSurfaceChunkCoordsInBounds(BrushBounds);
	
    for (const FIntVector2& ChunkCoord : SurfaceChunkCoords)
    {
        const FLandscapeChunkKey Key{ChunkCoord, SurfaceLayerIndex};
		LandscapeGrid.ApplyBrush(Key, LayerName, FVector2f(WorldPos.X, WorldPos.Y), Radius, Value);
    }
}

void UVoxelLandscapeSubsystem::ApplyCapsuleBrush(
	FName LayerName, FVector Start, FVector End, float Radius, float Value, FFloatInterval ValueRange, int32 SurfaceLayerIndex)
{
	checkf(!IsSimulating(), TEXT("ApplyCapsuleBrush called while simulation is in-flight"));
	if (!SurfaceSubsystem.IsValid())
	{
		return;
	}

	const FVector2D BoundsMin(FMath::Min(Start.X, End.X) - Radius, FMath::Min(Start.Y, End.Y) - Radius);
	const FVector2D BoundsMax(FMath::Max(Start.X, End.X) + Radius, FMath::Max(Start.Y, End.Y) + Radius);
	const TArray<FIntVector2> SurfaceChunkCoords = SurfaceSubsystem->GetSurfaceChunkCoordsInBounds(FBox2D(BoundsMin, BoundsMax));
	for (const FIntVector2& ChunkCoord : SurfaceChunkCoords)
	{
		LandscapeGrid.ApplyCapsuleBrush({ChunkCoord, SurfaceLayerIndex}, LayerName,
			FVector2f(Start.X, Start.Y), FVector2f(End.X, End.Y),
			Radius, Value, ValueRange);
	}
}

void UVoxelLandscapeSubsystem::ClearAllCells()
{
	LandscapeGrid.ClearAllCells();
}

void UVoxelLandscapeSubsystem::NotifySurfaceUpdated(const TArray<FIntVector2>& UpdatedChunks)
{
	LandscapeGrid.MarkChunksDirty(UpdatedChunks);
}

bool UVoxelLandscapeSubsystem::IsStateless() const
{
	return PendingOutputs.IsEmpty() && PendingEvents.IsEmpty() && LastDirtyChunks.IsEmpty() && LastDirtyChunkKeys.IsEmpty();
}

void UVoxelLandscapeSubsystem::SpillToLowerLayers()
{
	const UVoxelDeveloperSettings* Settings = GetDefault<UVoxelDeveloperSettings>();
    const int32 ColRes = VoxelLandscape::Utils::GetColumnResolution();
	const TArray<FLandscapeChunkKey> SimulatedKeys = LastDirtyChunkKeys;
	for (const FLandscapeChunkKey& Key : SimulatedKeys)
    {
		FVoxelLandscapeChunk* Chunk = LandscapeGrid.Find(Key);
		if (!Chunk) continue;
        const FLandscapeChunkKey LowerKey{Key.ChunkCoord, Key.SurfaceLayerIndex + 1};
        FVoxelLandscapeChunk* LowerChunk = LandscapeGrid.Find(LowerKey);
        if (!LowerChunk) continue;

        for (auto& [LayerName, Layer] : Chunk->Layers)
        {
            const FLandscapeLayerConfig* LayerConfig = Settings->GetLandscapeLayerConfig(LayerName);
            if (!LayerConfig || LayerConfig->FluidParams.SpillToLowerLayer <= 0.f) continue;

            FVoxelLandscapeLayer* LowerLayer = LowerChunk->Layers.Find(LayerName);
            if (!LowerLayer) continue;

			bool bDidSpill = false;
            for (int32 i = 0; i < Layer.Cells.Num(); ++i)
            {
                const int32 cx = i % ColRes;
                const int32 cy = i / ColRes;
                if (cx != 1 && cx != ColRes - 2 && cy != 1 && cy != ColRes - 2) continue;
                if (Layer.Cells[i].Depth <= 0.f) continue;

                const float Spill = LayerConfig->FluidParams.SpillToLowerLayer * Layer.Cells[i].Depth;
				if (Spill <= LayerConfig->FluidParams.SleepDepthThreshold) continue;
				bDidSpill = true;
                const FVector2f SpillVelocity = Layer.Cells[i].Velocity;
                Layer.Cells[i].Depth -= Spill;
                if (LowerLayer->Cells.IsValidIndex(i))
                {
                    FLandscapeCell& LowerCell = LowerLayer->Cells[i];
                    const float ExistingDepth = LowerCell.Depth;
                    LowerCell.Depth += Spill;
                    if (LowerCell.Depth > UE_SMALL_NUMBER)
                    {
                        LowerCell.Velocity = (LowerCell.Velocity * ExistingDepth + SpillVelocity * Spill) / LowerCell.Depth;
                    }
                }
            }
			if (bDidSpill)
			{
				LandscapeGrid.MarkDirtyAndWake(Key);
				LandscapeGrid.MarkDirtyAndWake(LowerKey);
			}
        }
    }
}

void UVoxelLandscapeSubsystem::MergeBorderCells(const TArray<FIntVector2>& DirtyChunks)
{
	TSet<FLandscapeChunkKey> KeysToSync;
	const TSet<FLandscapeChunkKey>& DirtyKeys = LandscapeGrid.GetDirtyKeySet();
	KeysToSync.Reserve(DirtyChunks.Num() + DirtyKeys.Num());
	for (const FIntVector2& DirtyChunk : DirtyChunks)
	{
		const FLandscapeChunkKeyList* Keys = LandscapeGrid.FindKeys(DirtyChunk);
		if (!Keys) continue;

		for (const FLandscapeChunkKey& Key : Keys->Values)
		{
			KeysToSync.Add(Key);
		}
	}

	for (const FLandscapeChunkKey& Key : DirtyKeys)
	{
		KeysToSync.Add(Key);
	}

    for (const FLandscapeChunkKey& Key : KeysToSync)
    {
        FVoxelLandscapeChunk* Chunk = LandscapeGrid.Find(Key);
        if (!Chunk) continue;

        for (auto& [LayerName, Layer] : Chunk->Layers)
        {
            CopyBorderFromNeighbors(LayerName, Key.ChunkCoord, Key.SurfaceLayerIndex, Layer.Cells);
        }
    }
}
