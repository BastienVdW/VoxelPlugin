// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Streaming/VoxelView.h"
#include "Grid/VoxelGrid.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelView_DefaultValues,
	"VoxelPlugin.Streaming.ViewDefaultValues",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelView_DefaultValues::RunTest(const FString& Parameters)
{
	FVoxelView View;
	TestTrue("NearRadius > 0",    View.NearRadius > 0.f);
	TestTrue("FarRadius > Near",  View.FarRadius > View.NearRadius);
	TestTrue("NearVoxelSize > 0", View.NearVoxelSize > 0.f);
	TestTrue("FarVoxelSize > Near", View.FarVoxelSize >= View.NearVoxelSize);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGrid_WorldToChunkCoord,
	"VoxelPlugin.Grid.WorldToChunkCoord",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelGrid_WorldToChunkCoord::RunTest(const FString& Parameters)
{
	FVoxelGrid Grid; // default: 16 voxels * 25cm = 400cm per chunk

	TestEqual("Origin -> chunk (0,0,0)",
		Grid.WorldToChunkCoord(FVector(0,0,0)), FIntVector(0,0,0));
	TestEqual("(399,399,399) -> chunk (0,0,0)",
		Grid.WorldToChunkCoord(FVector(399,399,399)), FIntVector(0,0,0));
	TestEqual("(400,0,0) -> chunk (1,0,0)",
		Grid.WorldToChunkCoord(FVector(400,0,0)), FIntVector(1,0,0));
	TestEqual("Negative -> chunk (-1,-1,-1)",
		Grid.WorldToChunkCoord(FVector(-1,-1,-1)), FIntVector(-1,-1,-1));

	return true;
}
