// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "VoxelLandscapeTypes.h"
#include "Async/TaskGraphInterfaces.h"
#include "Simulation/LandscapeSimTypes.h"
#include "Grid/VoxelLandscapeGrid.h"

#include "VoxelLandscapeSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLandscapeSimFlushed, const TArray<FIntVector2>& /*LastDirtyChunks*/);

struct FLandscapePendingOutputKey
{
	FIntVector2 ChunkCoord;
	int32 SurfaceLayerIndex = 0;
	FName LayerName;

	bool operator==(const FLandscapePendingOutputKey& Other) const
	{
		return ChunkCoord == Other.ChunkCoord &&
			SurfaceLayerIndex == Other.SurfaceLayerIndex &&
			LayerName == Other.LayerName;
	}
};

inline uint32 GetTypeHash(const FLandscapePendingOutputKey& Key)
{
	return HashCombine(HashCombine(GetTypeHash(Key.ChunkCoord), GetTypeHash(Key.SurfaceLayerIndex)), GetTypeHash(Key.LayerName));
}

UCLASS()
class VOXELLANDSCAPE_API UVoxelLandscapeSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // Called at PrePhysics. Snapshots surface data, dispatches sim tasks for dirty chunks.
    void StartSimulation();

    // Called at FrameEnd (VoxelLandscapeFlush, after VoxelSurfaceFlush).
    // Blocks on tasks, commits cross-chunk transfers, and syncs border cells.
    void ForceEndSimulation();
	
	bool IsSimulating() const { return bIsSimulating; }

    const FVoxelLandscapeChunk* GetChunk(FIntVector2 ChunkCoord, int32 SurfaceLayerIndex) const;

    const TMap<FLandscapeChunkKey, FVoxelLandscapeChunk>& GetAllChunks() const { return LandscapeGrid.GetChunks(); }
	const FVoxelLandscapeGridData& GetGridData() const { return LandscapeGrid.GetData(); }
	void SetGridData(const FVoxelLandscapeGridData& Data) { LandscapeGrid.SetData(Data); }
    const TArray<FIntVector2>& GetLastDirtyChunks() const { return LastDirtyChunks; }

    void SetCellValue(FName LayerName, FIntVector2 ChunkCoord, int32 SurfaceLayerIndex,
                      FIntVector2 ColumnCoord, float Value);

    // Paint fluid brush: enumerate all columns within radius, add value to landscape cells.
    // Auto-creates landscape chunks for surfaces that exist at the brush position.
    // checkf(!bIsSimulating) - must be called after simulation ends, not mid-frame.
    void ApplyBrush(FName LayerName, FVector WorldPos, float Radius, float Value, int32 SurfaceLayerIndex = 0);
	void ApplyCapsuleBrush(FName LayerName, FVector Start, FVector End, float Radius, float Value,
		FFloatInterval ValueRange, int32 SurfaceLayerIndex = 0);

    void ClearAllCells();
    void NotifySurfaceUpdated(const TArray<FIntVector2>& UpdatedChunks);

	bool IsStateless() const;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    FOnLandscapeSimFlushed OnSimFlushed;

private:
	void WaitForPendingSimulation();
	void CommitPendingOutputs();
	void UpdateSimulationActivity();
    void CommitBorderTransfers();
    void MergeBorderCells(const TArray<FIntVector2>& DirtyChunks);
    void SpillToLowerLayers();
	void AddDepthToLayer(const TMap<FLandscapePendingOutputKey, FLandscapeSimOutput*>& PendingOutputByKey,
						  const FLandscapeChunkKey& Key, FName LayerName, int32 Index, float Depth, FVector2f Velocity, int32 CellCount);
    void CopyBorderFromNeighbors(FName LayerName, FIntVector2 ChunkCoord,
                                  int32 SurfLayerIdx, TArray<FLandscapeCell>& InOutCells) const;

	FVoxelLandscapeGrid LandscapeGrid;

    // In-flight task outputs (pre-allocated before dispatch)
    TArray<FLandscapeSimOutput>  PendingOutputs;
    FGraphEventArray             PendingEvents;
    TArray<FIntVector2>          LastDirtyChunks;
	TArray<FLandscapeChunkKey>	 LastDirtyChunkKeys;
	bool                         bIsSimulating = false;
	
    // Cached surface subsystem for ApplyBrush to look up chunk existence
    TWeakObjectPtr<class UVoxelSurfaceSubsystem> SurfaceSubsystem;
};
