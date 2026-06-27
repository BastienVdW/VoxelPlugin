// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "VoxelLandscapeTypes.h"

struct VOXELLANDSCAPE_API FLandscapeChunkKeyList
{
	TArray<FLandscapeChunkKey> Values;
};

// Owns landscape chunk storage and the indexes used to schedule simulation and rendering.
// This is deliberately a plain value type: the world subsystem owns lifecycle and orchestration.
class VOXELLANDSCAPE_API FVoxelLandscapeGrid
{
public:
	FVoxelLandscapeChunk* Find(const FLandscapeChunkKey& Key);
	const FVoxelLandscapeChunk* Find(const FLandscapeChunkKey& Key) const;
	FVoxelLandscapeChunk& FindOrAdd(const FLandscapeChunkKey& Key);
	float GetCellDepth(const FLandscapeChunkKey& Key, const FName LayerName, const int32 CellIndex) const;
	FVoxelLandscapeLayer& SpawnLayer(const FLandscapeChunkKey& Key, FName LayerName, int32 CellCount, float InitialValue);
	bool ApplyBrush(const FLandscapeChunkKey& Key, FName LayerName, FVector2f WorldPosition, float Radius, float Value);
	bool ApplyCapsuleBrush(const FLandscapeChunkKey& Key, FName LayerName, FVector2f Start, FVector2f End,
		float Radius, float Value, FFloatInterval ValueRange);
	bool AddDepthToLayer(const FLandscapeChunkKey& Key, FName LayerName, int32 CellIndex,
		float Depth, FVector2f Velocity, int32 CellCount);
	void ClearAllCells();

	const TMap<FLandscapeChunkKey, FVoxelLandscapeChunk>& GetChunks() const { return Data.Chunks; }
	const FLandscapeChunkKeyList* FindKeys(FIntVector2 ChunkCoord) const;
	TArray<FLandscapeChunkKey> GetDirtyKeys() const { return Data.DirtyKeys.Array(); }
	TArray<FIntVector2> GetDirtyChunkCoords() const;
	TArray<FLandscapeChunkKey> GetAwakeKeys() const;
	const TSet<FLandscapeChunkKey>& GetDirtyKeySet() const { return Data.DirtyKeys; }

	void MarkChunksDirty(TArrayView<const FIntVector2> ChunkCoords);
	void MarkDirty(const FLandscapeChunkKey& Key);
	void Wake(const FLandscapeChunkKey& Key);
	void MarkDirtyAndWake(const FLandscapeChunkKey& Key);
	void Sleep(const FLandscapeChunkKey& Key);
	void ClearDirty();

	const FVoxelLandscapeGridData& GetData() const { return Data; }
	void SetData(const FVoxelLandscapeGridData& InData);

private:
	void Register(const FLandscapeChunkKey& Key);
	void RebuildIndexes();
	void WakeNeighbors(const FLandscapeChunkKey& Key);

	FVoxelLandscapeGridData Data;
	TMap<FIntVector2, FLandscapeChunkKeyList> KeysByCoord;
};
