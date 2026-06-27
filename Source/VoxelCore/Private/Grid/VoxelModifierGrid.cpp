// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Grid/VoxelModifierGrid.h"

void FVoxelModifierGrid::Add(uint32 Id, const FVoxelModifierData& Data)
{
    DataById.Add(Id, Data);

    FVoxelModifierGridItem Item;
    Item.ModifierId = Id;
    Item.Bounds3D   = Data.GetWorldBounds().GetBox();

    Grid2D.Add(Item, ToXYBounds(Item.Bounds3D));
}

void FVoxelModifierGrid::Remove(uint32 Id, const FVoxelModifierData& /*Data*/)
{
    DataById.Remove(Id);
    RebuildGrid();
}

const FVoxelModifierData* FVoxelModifierGrid::GetById(uint32 Id) const
{
    return DataById.Find(Id);
}

bool FVoxelModifierGrid::HasModifiersAtChunk(FIntVector ChunkCoord, float ChunkWorldSize) const
{
    const FBox Box3D  = ChunkBox(ChunkCoord, ChunkWorldSize);
    return !GetSortedModifierItemsInBounds(Box3D).IsEmpty();
}

TArray<FVoxelModifierData> FVoxelModifierGrid::GetModifiersForChunk(
    FIntVector ChunkCoord, float ChunkWorldSize, float BorderWorldSize) const
{
	const FBox Box3D = ChunkBoxWithBorder(ChunkCoord, ChunkWorldSize, BorderWorldSize);
	const TArray<FVoxelModifierGridItem> Items = GetSortedModifierItemsInBounds(Box3D, true);
	
	TArray<FVoxelModifierData> Result;
	Result.Reserve(Items.Num());
	Algo::Transform(Items, Result, [&](const FVoxelModifierGridItem& Item)
	{
		return DataById.FindChecked(Item.ModifierId);
	});
	
    return Result;
}

TArray<FVoxelModifierGridItem> FVoxelModifierGrid::GetModifierItemsForChunk(FIntVector ChunkCoord, float ChunkWorldSize, float BorderWorldSize) const
{
	const FBox Box3D = ChunkBoxWithBorder(ChunkCoord, ChunkWorldSize, BorderWorldSize);
	return GetSortedModifierItemsInBounds(Box3D);
}

TArray<FModifierHandle> FVoxelModifierGrid::GetModifierHandlesInBounds(const FBox& WorldBox3D) const
{
    const TArray<FVoxelModifierGridItem> Items = GetSortedModifierItemsInBounds(WorldBox3D);
    TArray<FModifierHandle> Result;
    Result.Reserve(Items.Num());
    Algo::Transform(Items, Result, [](const FVoxelModifierGridItem& Item)
    {
        return FModifierHandle(Item.ModifierId);
    });
    return Result;
}

TSet<FIntVector> FVoxelModifierGrid::GetChunksWithModifiersInRegion(
    const FBox& WorldBox3D, float ChunkWorldSize) const
{
    const TArray<FVoxelModifierGridItem> Items = GetSortedModifierItemsInBounds(WorldBox3D);
    TSet<FIntVector> Result;

    for (const FVoxelModifierGridItem& Item : Items)
    {
        const FBox Intersection = Item.Bounds3D.Overlap(WorldBox3D);
        if (!Intersection.IsValid) continue;

        // Enumerate every chunk coord the intersection touches
        const auto ToChunk = [ChunkWorldSize](float V) { return FMath::FloorToInt(V / ChunkWorldSize); };
        const FIntVector Min(ToChunk(Intersection.Min.X), ToChunk(Intersection.Min.Y), ToChunk(Intersection.Min.Z));
        const FIntVector Max(ToChunk(Intersection.Max.X - KINDA_SMALL_NUMBER),
                             ToChunk(Intersection.Max.Y - KINDA_SMALL_NUMBER),
                             ToChunk(Intersection.Max.Z - KINDA_SMALL_NUMBER));
        for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
        for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
        for (int32 X = Min.X; X <= Max.X; ++X)
            Result.Add(FIntVector(X, Y, Z));
    }

    return Result;
}

TArray<FVoxelModifierData> FVoxelModifierGrid::GetModifiersAtXY(
    FIntVector2 ColumnCoord, float ChunkWorldSize, float BorderWorldSize) const
{
    const FVector OriginXY = FVector(ColumnCoord.X, ColumnCoord.Y, 0.0) * ChunkWorldSize;
    const FBox QueryXY = FBox(
        FVector(OriginXY.X - BorderWorldSize, OriginXY.Y - BorderWorldSize, 0.0),
        FVector(OriginXY.X + ChunkWorldSize + BorderWorldSize, OriginXY.Y + ChunkWorldSize + BorderWorldSize, 0.0)
    );

    const TArray<FVoxelModifierGridItem> Items = GetSortedModifierItemsInBounds(QueryXY, false);

    TArray<FVoxelModifierData> Result;
    Result.Reserve(Items.Num());
    Algo::Transform(Items, Result, [&](const FVoxelModifierGridItem& Item)
    {
        return DataById.FindChecked(Item.ModifierId);
    });

    return Result;
}

void FVoxelModifierGrid::RebuildGrid()
{
    Grid2D.Reset();
    for (const auto& [Id, Data] : DataById)
    {
        FVoxelModifierGridItem Item;
        Item.ModifierId = Id;
        Item.Bounds3D   = Data.GetWorldBounds().GetBox();
        Grid2D.Add(Item, ToXYBounds(Item.Bounds3D));
    }
}

FBox FVoxelModifierGrid::ToXYBounds(const FBox& Box3D)
{
    return FBox(
        FVector(Box3D.Min.X, Box3D.Min.Y, 0.f),
        FVector(Box3D.Max.X, Box3D.Max.Y, 0.f)
    );
}

FBox FVoxelModifierGrid::ChunkBox(FIntVector ChunkCoord, float ChunkWorldSize)
{
    const FVector Origin = FVector(ChunkCoord) * ChunkWorldSize;
    return FBox(Origin, Origin + FVector(ChunkWorldSize));
}

FBox FVoxelModifierGrid::ChunkBoxWithBorder(
    FIntVector ChunkCoord, float ChunkWorldSize, float BorderWorldSize)
{
    const FVector Origin = FVector(ChunkCoord) * ChunkWorldSize;
    return FBox(
        Origin - FVector(BorderWorldSize),
        Origin + FVector(ChunkWorldSize + BorderWorldSize)
    );
}

TArray<FVoxelModifierGridItem> FVoxelModifierGrid::GetSortedModifierItemsInBounds(
    const FBox& WorldBox3D, bool bRequire3DIntersection/*= true*/) const
{
    const FBox QueryXY = ToXYBounds(WorldBox3D);

    TArray<FVoxelModifierGridItem> CandidateItems;
    Grid2D.QuerySmall(QueryXY, CandidateItems);

    TArray<FVoxelModifierGridItem> Items;
    TSet<uint32> Visited;

    for (const FVoxelModifierGridItem& Item : CandidateItems)
    {
        if (!Visited.Contains(Item.ModifierId) &&
            (!bRequire3DIntersection || WorldBox3D.Intersect(Item.Bounds3D)))
        {
            Items.Add(Item);
            Visited.Add(Item.ModifierId);
        }
    }

    Items.Sort([](const FVoxelModifierGridItem& Left, const FVoxelModifierGridItem& Right)
    {
        return Left.ModifierId < Right.ModifierId;
    });

    return Items;
}
