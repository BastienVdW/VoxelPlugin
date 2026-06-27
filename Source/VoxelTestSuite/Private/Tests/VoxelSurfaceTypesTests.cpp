// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelSurfaceTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurface_ChunkDefaults,
	"VoxelPlugin.Surface.ChunkDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelSurface_ChunkDefaults::RunTest(const FString& Parameters)
{
	FVoxelSurfaceChunk Chunk;
	Chunk.ChunkCoord = FIntVector2(3, -2);

	TestFalse("New chunk is not dirty", Chunk.bDirty);
	TestTrue("Columns array is empty", Chunk.Columns.IsEmpty());
	TestEqual("ChunkCoord X", Chunk.ChunkCoord.X, 3);

	FVoxelSurfaceColumn Col;
	Col.ColumnCoord = FIntVector2(1, 2);
	TestTrue("New column has no levels", Col.Levels.IsEmpty());

	FVoxelSurfaceLevel Level;
	Level.WorldZ = 100.f;
	Level.Normal = FVector::UpVector;
	Level.LayerIndex = 0;
	TestEqual("Level WorldZ", Level.WorldZ, 100.f);
	TestEqual("Level LayerIndex", Level.LayerIndex, 0);

	return true;
}
