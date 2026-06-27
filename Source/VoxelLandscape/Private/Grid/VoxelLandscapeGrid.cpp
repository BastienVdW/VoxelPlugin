// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Grid/VoxelLandscapeGrid.h"

#include "Utils/VoxelLandscapeUtils.h"

FVoxelLandscapeChunk* FVoxelLandscapeGrid::Find(const FLandscapeChunkKey& Key)
{
	return Data.Chunks.Find(Key);
}

const FVoxelLandscapeChunk* FVoxelLandscapeGrid::Find(const FLandscapeChunkKey& Key) const
{
	return Data.Chunks.Find(Key);
}

FVoxelLandscapeChunk& FVoxelLandscapeGrid::FindOrAdd(const FLandscapeChunkKey& Key)
{
	Register(Key);
	FVoxelLandscapeChunk& Chunk = Data.Chunks.FindOrAdd(Key);
	Chunk.ChunkCoord = Key.ChunkCoord;
	Chunk.SurfaceLayerIndex = Key.SurfaceLayerIndex;
	return Chunk;
}

FVoxelLandscapeLayer& FVoxelLandscapeGrid::SpawnLayer(
	const FLandscapeChunkKey& Key, const FName LayerName, const int32 CellCount, const float InitialValue)
{
	FVoxelLandscapeLayer& Layer = FindOrAdd(Key).Layers.FindOrAdd(LayerName);
	Layer.Cells.SetNum(CellCount);
	for (FLandscapeCell& Cell : Layer.Cells)
	{
		Cell.Depth = InitialValue;
		Cell.Velocity = FVector2f::ZeroVector;
	}
	MarkDirtyAndWake(Key);
	return Layer;
}

bool FVoxelLandscapeGrid::ApplyBrush(
	const FLandscapeChunkKey& Key, const FName LayerName, const FVector2f WorldPosition, const float Radius, const float Value)
{
	const int32 ColumnResolution = VoxelLandscape::Utils::GetColumnResolution();
	const auto* Settings = GetDefault<UVoxelDeveloperSettings>();
	const float VoxelSize = Settings->VoxelSize;
	const float ChunkWorldSize = Settings->ChunkResolution * VoxelSize;

	const int32 CellCount = ColumnResolution * ColumnResolution;
	FVoxelLandscapeChunk* Chunk = Find(Key);
	FVoxelLandscapeLayer* Layer = Chunk ? Chunk->Layers.Find(LayerName) : nullptr;
	if (!Layer)
	{
		Layer = &SpawnLayer(Key, LayerName, CellCount, 0.f);
	}
	if (Layer->Cells.Num() != CellCount)
	{
		return false;
	}

	const FVector2f ChunkOrigin(Key.ChunkCoord.X * ChunkWorldSize, Key.ChunkCoord.Y * ChunkWorldSize);
	const float RadiusSquared = Radius * Radius;
	bool bChanged = false;
	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		const FVector2f ColumnPosition = ChunkOrigin - FVector2f(VoxelSize, VoxelSize) +
			FVector2f(Index % ColumnResolution, Index / ColumnResolution) * VoxelSize;
		if ((ColumnPosition - WorldPosition).SizeSquared() <= RadiusSquared)
		{
			Layer->Cells[Index].Depth += Value;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		MarkDirtyAndWake(Key);
	}
	return bChanged;
}

bool FVoxelLandscapeGrid::ApplyCapsuleBrush(
	const FLandscapeChunkKey& Key,
	const FName LayerName,
	const FVector2f Start,
	const FVector2f End,
	const float Radius,
	const float Value,
	const FFloatInterval ValueRange)
{
	const int32 ColumnResolution = VoxelLandscape::Utils::GetColumnResolution();
	const auto* Settings = GetDefault<UVoxelDeveloperSettings>();
	const float VoxelSize = Settings->VoxelSize;
	const float ChunkWorldSize = Settings->ChunkResolution * VoxelSize;
	const int32 CellCount = ColumnResolution * ColumnResolution;

	FVoxelLandscapeChunk* Chunk = Find(Key);
	FVoxelLandscapeLayer* Layer = Chunk ? Chunk->Layers.Find(LayerName) : nullptr;
	if (!Layer)
	{
		// A missing layer has an implicit depth of zero. A subtractive brush must not create it.
		if (Value <= 0.f)
		{
			return false;
		}
		Layer = &SpawnLayer(Key, LayerName, CellCount, 0.f);
	}
	if (Layer->Cells.Num() != CellCount)
	{
		return false;
	}

	const FVector2f Segment = End - Start;
	const float SegmentLengthSquared = Segment.SizeSquared();
	const FVector2f ChunkOrigin(Key.ChunkCoord.X * ChunkWorldSize, Key.ChunkCoord.Y * ChunkWorldSize);
	const float RadiusSquared = FMath::Square(Radius);
	bool bChanged = false;
	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		const FVector2f ColumnPosition = ChunkOrigin - FVector2f(VoxelSize, VoxelSize) +
			FVector2f(Index % ColumnResolution, Index / ColumnResolution) * VoxelSize;
		const float Alpha = SegmentLengthSquared > UE_SMALL_NUMBER
			? FMath::Clamp(FVector2f::DotProduct(ColumnPosition - Start, Segment) / SegmentLengthSquared, 0.f, 1.f)
			: 0.f;
		if ((ColumnPosition - (Start + Segment * Alpha)).SizeSquared() > RadiusSquared)
		{
			continue;
		}

		FLandscapeCell& Cell = Layer->Cells[Index];
		float NewDepth = Cell.Depth;
		if (Value < 0.f && Cell.Depth >= ValueRange.Min)
		{
			NewDepth = FMath::Max(Cell.Depth + Value, ValueRange.Min);
		}
		else if (Value > 0.f && Cell.Depth <= ValueRange.Max)
		{
			NewDepth = FMath::Min(Cell.Depth + Value, ValueRange.Max);
		}
		if (!FMath::IsNearlyEqual(NewDepth, Cell.Depth))
		{
			Cell.Depth = NewDepth;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		MarkDirtyAndWake(Key);
	}
	return bChanged;
}

bool FVoxelLandscapeGrid::AddDepthToLayer(
	const FLandscapeChunkKey& Key,
	const FName LayerName,
	const int32 CellIndex,
	const float Depth,
	const FVector2f Velocity,
	const int32 CellCount)
{
	if (Depth <= 0.f || CellIndex < 0 || CellIndex >= CellCount)
	{
		return false;
	}

	FVoxelLandscapeChunk* Chunk = Find(Key);
	FVoxelLandscapeLayer* Layer = Chunk ? Chunk->Layers.Find(LayerName) : nullptr;
	if (!Layer)
	{
		Layer = &SpawnLayer(Key, LayerName, CellCount, 0.f);
	}
	if (Layer->Cells.Num() != CellCount)
	{
		return false;
	}

	FLandscapeCell& Cell = Layer->Cells[CellIndex];
	const float ExistingDepth = Cell.Depth;
	Cell.Depth += Depth;
	if (Cell.Depth > UE_SMALL_NUMBER)
	{
		Cell.Velocity = (Cell.Velocity * ExistingDepth + Velocity * Depth) / Cell.Depth;
	}
	MarkDirtyAndWake(Key);
	return true;
}

void FVoxelLandscapeGrid::ClearAllCells()
{
	Data.Reset();
	KeysByCoord.Reset();
}

void FVoxelLandscapeGrid::SetData(const FVoxelLandscapeGridData& InData)
{
	Data = InData;
	RebuildIndexes();
}

void FVoxelLandscapeGrid::MarkChunksDirty(const TArrayView<const FIntVector2> ChunkCoords)
{
	TSet<FIntVector2> UniqueChunkCoords;
	UniqueChunkCoords.Reserve(ChunkCoords.Num());
	for (const FIntVector2 ChunkCoord : ChunkCoords)
	{
		UniqueChunkCoords.Add(ChunkCoord);
	}

	for (const FIntVector2 ChunkCoord : UniqueChunkCoords)
	{
		const FLandscapeChunkKeyList* Keys = FindKeys(ChunkCoord);
		if (!Keys) continue;

		for (const FLandscapeChunkKey& Key : Keys->Values)
		{
			MarkDirtyAndWake(Key);
		}
	}
}

const FLandscapeChunkKeyList* FVoxelLandscapeGrid::FindKeys(const FIntVector2 ChunkCoord) const
{
	return KeysByCoord.Find(ChunkCoord);
}

float FVoxelLandscapeGrid::GetCellDepth(
	const FLandscapeChunkKey& Key, const FName LayerName, const int32 CellIndex) const
{
	const FVoxelLandscapeChunk* Chunk = Find(Key);
	if (!Chunk) return 0.f;

	const FVoxelLandscapeLayer* Layer = Chunk->Layers.Find(LayerName);
	if (!Layer || !Layer->Cells.IsValidIndex(CellIndex)) return 0.f;

	return Layer->Cells[CellIndex].Depth;
}

TArray<FIntVector2> FVoxelLandscapeGrid::GetDirtyChunkCoords() const
{
	TSet<FIntVector2> UniqueCoords;
	UniqueCoords.Reserve(Data.DirtyKeys.Num());
	for (const FLandscapeChunkKey& Key : Data.DirtyKeys)
	{
		UniqueCoords.Add(Key.ChunkCoord);
	}
	return UniqueCoords.Array();
}

TArray<FLandscapeChunkKey> FVoxelLandscapeGrid::GetAwakeKeys() const
{
	TArray<FLandscapeChunkKey> AwakeKeys;
	AwakeKeys.Reserve(Data.Chunks.Num());
	for (const auto& [Key, Chunk] : Data.Chunks)
	{
		if (!Chunk.bSleeping)
		{
			AwakeKeys.Add(Key);
		}
	}
	return AwakeKeys;
}

void FVoxelLandscapeGrid::MarkDirty(const FLandscapeChunkKey& Key)
{
	if (FVoxelLandscapeChunk* Chunk = Data.Chunks.Find(Key))
	{
		Chunk->bDirty = true;
		Data.DirtyKeys.Add(Key);
		Register(Key);
	}
}

void FVoxelLandscapeGrid::Wake(const FLandscapeChunkKey& Key)
{
	if (FVoxelLandscapeChunk* Chunk = Data.Chunks.Find(Key))
	{
		Chunk->StableSimulationSteps = 0;
		Chunk->bSleeping = false;
		Register(Key);
	}
}

void FVoxelLandscapeGrid::MarkDirtyAndWake(const FLandscapeChunkKey& Key)
{
	MarkDirty(Key);
	Wake(Key);
	WakeNeighbors(Key);
}

void FVoxelLandscapeGrid::Sleep(const FLandscapeChunkKey& Key)
{
	if (FVoxelLandscapeChunk* Chunk = Data.Chunks.Find(Key))
	{
		Chunk->bSleeping = true;
	}
}

void FVoxelLandscapeGrid::ClearDirty()
{
	for (const FLandscapeChunkKey& Key : Data.DirtyKeys)
	{
		if (FVoxelLandscapeChunk* Chunk = Data.Chunks.Find(Key))
		{
			Chunk->bDirty = false;
		}
	}
	Data.DirtyKeys.Reset();
}

void FVoxelLandscapeGrid::Register(const FLandscapeChunkKey& Key)
{
	KeysByCoord.FindOrAdd(Key.ChunkCoord).Values.AddUnique(Key);
}

void FVoxelLandscapeGrid::RebuildIndexes()
{
	KeysByCoord.Reset();
	KeysByCoord.Reserve(Data.Chunks.Num());
	for (const auto& [Key, Chunk] : Data.Chunks)
	{
		Register(Key);
	}
}

void FVoxelLandscapeGrid::WakeNeighbors(const FLandscapeChunkKey& Key)
{
	static const FIntVector2 Offsets[] =
	{
		FIntVector2(1, 0), FIntVector2(-1, 0), FIntVector2(0, 1), FIntVector2(0, -1)
	};
	for (const FIntVector2 Offset : Offsets)
	{
		Wake({Key.ChunkCoord + Offset, Key.SurfaceLayerIndex});
	}
}
