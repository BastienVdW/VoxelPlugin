// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Grid/VoxelGrid.h"
#include "Generation/VoxelChunkGenerator.h"
#include "Modifier/VoxelModifierTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGeneration_SphereFillsCenter,
	"VoxelPlugin.Generation.SphereFillsCenter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelGeneration_SphereFillsCenter::RunTest(const FString& Parameters)
{
	// Place a sphere at the center of chunk (0,0,0)
	// Chunk (0,0,0): world range [0..400cm]^3, center = (200,200,200)
	FVoxelGrid Grid;

	FVoxelModifierData Modifier;
	Modifier.Params.Type = EModifierType::PrimitiveSphere;
	Modifier.Params.Operation = EModifierOp::Add;
	Modifier.Transform.SetLocation(FVector(200.f, 200.f, 200.f));
	Modifier.Transform.SetScale3D(FVector(150.f)); // radius 150cm — covers chunk center
	Modifier.Params.SurfaceType = 1;

	Grid.AddModifier(Modifier);

	FVoxelChunkGenerator Generator;
	TArray<FIntVector> Dirty = Grid.GetDirtyChunks();
	Generator.StartGeneration(nullptr, Grid, Dirty);
	Generator.ForceEndGeneration(nullptr, Grid);

	// Voxel at exact center (8,8,8) should be solid
	FIntVector Center(8, 8, 8);
	const FVoxelChunk* Chunk = Grid.QueryChunk(FIntVector(0,0,0));
	TestNotNull("Chunk (0,0,0) exists", Chunk);
	if (Chunk)
	{
		const FVoxel& V = Chunk->At(8, 8, 8);
		TestTrue("Center voxel density > 0 (solid)", V.Density > 0.f);
		TestTrue("Center voxel IsSolid()", V.IsSolid());
		TestEqual("Center voxel SurfaceType == 1", (int32)V.SurfaceType, 1);
	}

	// Voxel at corner (0,0,0) should be empty (sphere doesn't reach corner)
	if (Chunk)
	{
		const FVoxel& Corner = Chunk->At(0, 0, 0);
		TestTrue("Corner voxel density <= 0 (empty)", Corner.Density <= 0.f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGeneration_CarveRemovesDensity,
	"VoxelPlugin.Generation.CarveRemovesDensity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelGeneration_CarveRemovesDensity::RunTest(const FString& Parameters)
{
	FVoxelGrid Grid;

	// Fill with large sphere
	FVoxelModifierData Fill;
	Fill.Params.Type = EModifierType::PrimitiveSphere;
	Fill.Params.Operation = EModifierOp::Add;
	Fill.Transform.SetLocation(FVector(200.f));
	Fill.Transform.SetScale3D(FVector(300.f)); // radius 300cm — fills all of chunk 0
	Grid.AddModifier(Fill);

	// Carve small sphere at center
	FVoxelModifierData Carve;
	Carve.Params.Type = EModifierType::PrimitiveSphere;
	Carve.Params.Operation = EModifierOp::Remove;
	Carve.Transform.SetLocation(FVector(200.f));
	Carve.Transform.SetScale3D(FVector(50.f)); // radius 50cm
	Grid.AddModifier(Carve);
	
	TArray<FIntVector> Dirty = Grid.GetDirtyChunks();

	FVoxelChunkGenerator Generator;
	Generator.StartGeneration(nullptr, Grid, Dirty);
	Generator.ForceEndGeneration(nullptr, Grid);

	const FVoxelChunk* Chunk = Grid.QueryChunk(FIntVector(0,0,0));
	TestNotNull("Chunk exists", Chunk);
	if (Chunk)
	{
		// Voxel at exact center (8,8,8) should be carved out (empty)
		TestTrue("Center carved — density <= 0", Chunk->At(8,8,8).Density <= 0.f);
		// Voxel near center but outside carve sphere should be solid
		TestTrue("Near-center solid", Chunk->At(8,8,12).Density > 0.f || Chunk->At(8,12,8).Density > 0.f);
	}

	return true;
}
