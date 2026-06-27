// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "Grid/VoxelGrid.h"

FVoxelGrid::FVoxelGrid(FVoxelGridConfig InConfig)
    : Config(InConfig)
{}

FModifierHandle FVoxelGrid::AddModifier(const FVoxelModifierData& Modifier, uint32 HandleId/*= 0*/)
{
    checkf(!bIsGenerating, TEXT("AddModifier called while async chunk generation is running — modifier mutations are not thread-safe during generation"));
    const uint32 Id = HandleId != 0 ? HandleId : NextModifierId++;
    ModifierGrid.Add(Id, Modifier);
    MarkChunksOverlappingBounds(Modifier.GetWorldBounds());
    return FModifierHandle{ Id };
}

void FVoxelGrid::RemoveModifier(FModifierHandle& Handle)
{
    checkf(!bIsGenerating, TEXT("RemoveModifier called while async chunk generation is running — modifier mutations are not thread-safe during generation"));
    const FVoxelModifierData* Data = ModifierGrid.GetById(Handle.Id);
    if (!Data) return;
    FBoxSphereBounds Bounds = Data->GetWorldBounds();
    ModifierGrid.Remove(Handle.Id, *Data);
	Handle.Invalidate();
    MarkChunksOverlappingBounds(Bounds);
}

const FVoxelModifierData* FVoxelGrid::GetModifier(const FModifierHandle& Handle) const
{
    return ModifierGrid.GetById(Handle.Id);
}

bool FVoxelGrid::HasModifiersAtChunk(FIntVector ChunkCoord) const
{
    return ModifierGrid.HasModifiersAtChunk(ChunkCoord, Config.DefaultResolution * Config.DefaultVoxelSize);
}

FVoxelChunk* FVoxelGrid::GetOrCreateChunk(FIntVector ChunkCoord)
{
    TSharedPtr<FVoxelChunk>* Found = Chunks.Find(ChunkCoord);
    if (Found) return Found->Get();

    TSharedPtr<FVoxelChunk> NewChunk = MakeShared<FVoxelChunk>();
    NewChunk->Init(ChunkCoord, Config.DefaultResolution, Config.DefaultVoxelSize, Config.DefaultBorderSize);
    FVoxelChunk* Ptr = NewChunk.Get();
    Chunks.Add(ChunkCoord, MoveTemp(NewChunk));

    if (ModifierGrid.HasModifiersAtChunk(ChunkCoord, Config.DefaultResolution * Config.DefaultVoxelSize))
    {
        Ptr->bDirty = true;
        DirtyChunkCoords.Add(ChunkCoord);
    }

    return Ptr;
}

FVoxelChunk* FVoxelGrid::FindChunk(FIntVector ChunkCoord)
{
    TSharedPtr<FVoxelChunk>* Found = Chunks.Find(ChunkCoord);
    return Found ? Found->Get() : nullptr;
}

const FVoxelChunk* FVoxelGrid::QueryChunk(FIntVector ChunkCoord) const
{
    const TSharedPtr<FVoxelChunk>* Found = Chunks.Find(ChunkCoord);
    return Found ? Found->Get() : nullptr;
}

void FVoxelGrid::EvictChunk(FIntVector ChunkCoord)
{
    DirtyChunkCoords.Remove(ChunkCoord);
    Chunks.Remove(ChunkCoord);
}

FVoxel FVoxelGrid::GetVoxel(FVector WorldPos) const
{
    FIntVector CC = WorldToChunkCoord(WorldPos);
    const FVoxelChunk* Chunk = QueryChunk(CC);
    if (!Chunk) return FVoxel{};
    FIntVector Local = WorldToLocalVoxel(WorldPos, CC);
    if (Local.X < 0 || Local.X >= Chunk->Resolution) return FVoxel{};
    if (Local.Y < 0 || Local.Y >= Chunk->Resolution) return FVoxel{};
    if (Local.Z < 0 || Local.Z >= Chunk->Resolution) return FVoxel{};
    return Chunk->At(Local.X, Local.Y, Local.Z);
}

TArray<FIntVector> FVoxelGrid::GetDirtyChunks() const
{
    return DirtyChunkCoords.Array();
}

void FVoxelGrid::ClearDirtyFlag(FIntVector ChunkCoord)
{
    DirtyChunkCoords.Remove(ChunkCoord);
    if (TSharedPtr<FVoxelChunk>* Found = Chunks.Find(ChunkCoord))
        (*Found)->bDirty = false;
}

TArray<FVoxelModifierData> FVoxelGrid::GetModifiersForChunk(const FIntVector& ChunkCoord) const
{
    const FVoxelChunk* Chunk = QueryChunk(ChunkCoord);
    if (!Chunk) return {};
    const float ChunkWorldSize = Config.DefaultResolution * Config.DefaultVoxelSize;
    const float BorderWorldSize = Config.DefaultBorderSize * Config.DefaultVoxelSize;
    return ModifierGrid.GetModifiersForChunk(ChunkCoord, ChunkWorldSize, BorderWorldSize);
}

TArray<FModifierHandle> FVoxelGrid::GetModifierHandlesForChunk(const FIntVector& ChunkCoord) const
{
	const FVoxelChunk* Chunk = QueryChunk(ChunkCoord);
	if (!Chunk) return {};
	const float ChunkWorldSize = Config.DefaultResolution * Config.DefaultVoxelSize;
	const float BorderWorldSize = Config.DefaultBorderSize * Config.DefaultVoxelSize;
	const TArray<FVoxelModifierGridItem> ModifierItems = ModifierGrid.GetModifierItemsForChunk(ChunkCoord, ChunkWorldSize, BorderWorldSize);
	
	TArray<FModifierHandle> Result;
	Result.Reserve(ModifierItems.Num());
	Algo::Transform(ModifierItems, Result, [](const FVoxelModifierGridItem& Item)
	{
		return FModifierHandle(Item.ModifierId);
	});
	return Result;
}

TArray<FModifierHandle> FVoxelGrid::GetModifierHandlesInBounds(const FBox& WorldBox3D) const
{
    return ModifierGrid.GetModifierHandlesInBounds(WorldBox3D);
}

TSet<FIntVector> FVoxelGrid::GetChunksWithModifiersInSphere(const FVector& Position, float Radius) const
{
    const FBox Box3D(Position - FVector(Radius), Position + FVector(Radius));
    const float ChunkWorldSize = Config.DefaultResolution * Config.DefaultVoxelSize;
    const TSet<FIntVector> Candidates = ModifierGrid.GetChunksWithModifiersInRegion(Box3D, ChunkWorldSize);

    // Sphere-filter by chunk center so behavior matches the old per-chunk distance check
    const float RadiusSq = Radius * Radius;
    const TArray<FIntVector> CandidatesInRange = Candidates.Array().FilterByPredicate([&](const FIntVector& Coord)
    {
        const FVector Center = FVector(Coord) * ChunkWorldSize + FVector(ChunkWorldSize * 0.5f);
        return FVector::DistSquared(Center, Position) <= RadiusSq;
    });
	
	return TSet<FIntVector>(CandidatesInRange);
}

TArray<FIntVector> FVoxelGrid::GetAllChunkCoords() const
{
    TArray<FIntVector> Result;
    Chunks.GetKeys(Result);
    return Result;
}

FIntVector FVoxelGrid::WorldToChunkCoord(FVector WorldPos) const
{
    float ChunkWorldSize = Config.DefaultResolution * Config.DefaultVoxelSize;
    return FIntVector(
        FMath::FloorToInt(WorldPos.X / ChunkWorldSize),
        FMath::FloorToInt(WorldPos.Y / ChunkWorldSize),
        FMath::FloorToInt(WorldPos.Z / ChunkWorldSize)
    );
}

FIntVector FVoxelGrid::WorldToLocalVoxel(FVector WorldPos, FIntVector ChunkCoord) const
{
    float ChunkWorldSize = Config.DefaultResolution * Config.DefaultVoxelSize;
    FVector LocalPos = WorldPos - FVector(ChunkCoord) * ChunkWorldSize;
    return FIntVector(
        FMath::FloorToInt(LocalPos.X / Config.DefaultVoxelSize),
        FMath::FloorToInt(LocalPos.Y / Config.DefaultVoxelSize),
        FMath::FloorToInt(LocalPos.Z / Config.DefaultVoxelSize)
    );
}

void FVoxelGrid::MarkChunksOverlappingBounds(const FBoxSphereBounds& Bounds)
{
    FBox Box = Bounds.GetBox();
    FIntVector Min = WorldToChunkCoord(Box.Min);
    FIntVector Max = WorldToChunkCoord(Box.Max);
    for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
    for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
    for (int32 X = Min.X; X <= Max.X; ++X)
    {
        FVoxelChunk* Chunk = GetOrCreateChunk(FIntVector(X, Y, Z));
        Chunk->bDirty = true;
        DirtyChunkCoords.Add(FIntVector(X, Y, Z));
    }
}
