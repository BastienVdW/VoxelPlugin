// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelLandscapeTypes.h"
#include "Simulation/LandscapeSimTask.h"
#include "Grid/VoxelLandscapeGrid.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_SpawnLayerPopulatesCells,
    "VoxelPlugin.Landscape.SpawnLayerPopulatesCells",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_SpawnLayerPopulatesCells::RunTest(const FString& Parameters)
{
    FVoxelLandscapeGrid Grid;
	const FLandscapeChunkKey Key{FIntVector2(0, 0), 0};
    const int32 ColRes = 18; // 16 + 2 border
    const float InitialValue = 5.f;
	Grid.SpawnLayer(Key, FName("Water"), ColRes * ColRes, InitialValue);
	FVoxelLandscapeChunk* Chunk = Grid.Find(Key);

	TestNotNull("Chunk exists", Chunk);
	if (!Chunk) return false;
	TestTrue("Water layer exists", Chunk->Layers.Contains(FName("Water")));
	TestEqual("Cell count", Chunk->Layers[FName("Water")].Cells.Num(), ColRes * ColRes);
	TestEqual("First cell depth", Chunk->Layers[FName("Water")].Cells[0].Depth, InitialValue);
	TestTrue("Spawned layer is dirty", Grid.GetDirtyKeys().Contains(Key));
	Grid.ApplyBrush(Key, FName("Water"), FVector2f::ZeroVector, 1.f, 2.f);
	TestEqual("Brush updates the grid-owned layer", Grid.Find(Key)->Layers[FName("Water")].Cells[1 + ColRes].Depth, InitialValue + 2.f);
	Grid.ClearAllCells();
	TestEqual("Clear resets depth", Grid.Find(Key)->Layers[FName("Water")].Cells[1 + ColRes].Depth, 0.f);
	Grid.AddDepthToLayer(Key, FName("Water"), 1 + ColRes, 4.f, FVector2f(2.f, 0.f), ColRes * ColRes);
	TestEqual("Depth transfer updates the grid-owned layer", Grid.Find(Key)->Layers[FName("Water")].Cells[1 + ColRes].Depth, 4.f);
	TestEqual("Depth transfer carries velocity", Grid.Find(Key)->Layers[FName("Water")].Cells[1 + ColRes].Velocity.X, 2.f);
	TestEqual("Grid reads cell depth", Grid.GetCellDepth(Key, FName("Water"), 1 + ColRes), 4.f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_GridSeparatesDirtyAndSleepingState,
	"VoxelPlugin.Landscape.Grid.SeparatesDirtyAndSleepingState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_GridSeparatesDirtyAndSleepingState::RunTest(const FString& Parameters)
{
	FVoxelLandscapeGrid Grid;
	const FLandscapeChunkKey Key{FIntVector2(2, -1), 3};
	Grid.FindOrAdd(Key);
	Grid.MarkDirtyAndWake(Key);

	TestTrue("Chunk is dirty after a mutation", Grid.GetDirtyKeys().Contains(Key));
	TestTrue("New chunk is awake by default", Grid.GetAwakeKeys().Contains(Key));
	TestTrue("Coordinate index contains the key", Grid.FindKeys(Key.ChunkCoord)->Values.Contains(Key));
	const FLandscapeChunkKey OtherLayerKey{Key.ChunkCoord, Key.SurfaceLayerIndex + 1};
	Grid.FindOrAdd(OtherLayerKey);
	Grid.MarkDirty(OtherLayerKey);
	TestEqual("Dirty coordinates are unique across surface layers", Grid.GetDirtyChunkCoords().Num(), 1);

	Grid.ClearDirty();
	TestFalse("Clearing render dirtiness does not stop simulation", Grid.GetDirtyKeys().Contains(Key));
	TestTrue("Chunk remains awake", Grid.GetAwakeKeys().Contains(Key));

	Grid.Sleep(Key);
	TestFalse("Settled chunk is no longer awake", Grid.GetAwakeKeys().Contains(Key));
	TestTrue("Settled chunk records sleeping state", Grid.Find(Key)->bSleeping);
	Grid.Wake(Key);
	TestFalse("Mutation wake clears sleeping state", Grid.Find(Key)->bSleeping);
	Grid.ClearDirty();
	Grid.Sleep(Key);
	const TArray<FIntVector2> UpdatedChunks{Key.ChunkCoord, Key.ChunkCoord};
	Grid.MarkChunksDirty(UpdatedChunks);
	TestTrue("Surface update marks indexed chunk dirty", Grid.GetDirtyKeys().Contains(Key));
	TestFalse("Surface update wakes indexed chunk", Grid.Find(Key)->bSleeping);
	TestNotNull("Settled chunk remains stored", Grid.Find(Key));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_CapsuleBrushRespectsShapeAndLimits,
	"VoxelPlugin.Landscape.Brush.CapsuleRespectsShapeAndLimits",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_CapsuleBrushRespectsShapeAndLimits::RunTest(const FString& Parameters)
{
	FVoxelLandscapeGrid Grid;
	const FLandscapeChunkKey Key{FIntVector2::ZeroValue, 0};
	const int32 ColRes = 18;
	Grid.SpawnLayer(Key, FName("Snow"), ColRes * ColRes, 20.f);

	Grid.ApplyCapsuleBrush(Key, FName("Snow"), FVector2f(0.f, 0.f), FVector2f(200.f, 0.f),
		10.f, -100.f, FFloatInterval(5.f, 100.f));
	FVoxelLandscapeChunk* Chunk = Grid.Find(Key);
	TestEqual("Capsule start is clamped to the minimum", Chunk->Layers[FName("Snow")].Cells[1 + ColRes].Depth, 5.f);
	TestEqual("Cell outside the capsule is unchanged", Chunk->Layers[FName("Snow")].Cells[1 + 2 * ColRes].Depth, 20.f);

	Chunk->Layers[FName("Snow")].Cells[1 + ColRes].Depth = 0.f;
	Grid.ApplyCapsuleBrush(Key, FName("Snow"), FVector2f::ZeroVector, FVector2f::ZeroVector,
		10.f, -100.f, FFloatInterval(5.f, 100.f));
	TestEqual("Value already below the minimum is not forced to the range", Chunk->Layers[FName("Snow")].Cells[1 + ColRes].Depth, 0.f);
	return true;
}
