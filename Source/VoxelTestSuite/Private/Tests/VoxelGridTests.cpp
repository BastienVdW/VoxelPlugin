// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Grid/VoxelGrid.h"
#include "Modifier/VoxelModifierTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGrid_AddModifierMarksDirty,
	"VoxelPlugin.Grid.AddModifierMarksDirty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelGrid_AddModifierMarksDirty::RunTest(const FString& Parameters)
{
	FVoxelGrid Grid;

	FVoxelModifierData Modifier;
	Modifier.Params.Type      = EModifierType::PrimitiveSphere;
	Modifier.Params.Operation = EModifierOp::Add;
	Modifier.Transform = FTransform(FVector(200.f, 200.f, 200.f));
	Modifier.Transform.SetScale3D(FVector(100.f)); // radius = 100cm
	Modifier.Params.SurfaceType = 1;

	FModifierHandle Handle = Grid.AddModifier(Modifier);
	TestTrue("Handle is valid after AddModifier", Handle.IsValid());

	TArray<FIntVector> Dirty = Grid.GetDirtyChunks();
	TestTrue("At least one chunk marked dirty", Dirty.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGrid_RemoveModifierMarksDirty,
	"VoxelPlugin.Grid.RemoveModifierMarksDirty",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelGrid_RemoveModifierMarksDirty::RunTest(const FString& Parameters)
{
	FVoxelGrid Grid;

	FVoxelModifierData Modifier;
	Modifier.Params.Type = EModifierType::PrimitiveSphere;
	Modifier.Params.Operation = EModifierOp::Add;
	Modifier.Transform.SetLocation(FVector(200.f));
	Modifier.Transform.SetScale3D(FVector(100.f));

	FModifierHandle Handle = Grid.AddModifier(Modifier);
	// Clear dirty flags to simulate post-generation state
	for (const FIntVector& C : Grid.GetDirtyChunks()) Grid.ClearDirtyFlag(C);
	TestEqual("No dirty after clear", Grid.GetDirtyChunks().Num(), 0);

	Grid.RemoveModifier(Handle);
	TestTrue("Dirty after remove", Grid.GetDirtyChunks().Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGrid_GetDirtyChunksAfterAdd,
	"VoxelPlugin.Grid.GetDirtyChunks_AfterAddModifier",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelGrid_GetDirtyChunksAfterAdd::RunTest(const FString& Parameters)
{
	FVoxelGrid Grid;
	FVoxelModifierData Mod;
	Mod.Params.Type = EModifierType::PrimitiveSphere;
	Mod.Params.Operation = EModifierOp::Add;
	Mod.Transform = FTransform(FVector(200.f, 200.f, 200.f)); // inside chunk (0,0,0)
	Grid.AddModifier(Mod);
	TArray<FIntVector> Dirty = Grid.GetDirtyChunks();
	TestTrue("At least one dirty chunk", Dirty.Num() > 0);
	TestTrue("Chunk (0,0,0) is dirty", Dirty.Contains(FIntVector(0, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGrid_GetModifiersForChunk,
	"VoxelPlugin.Grid.GetModifiersForChunk_ReturnsCorrectCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelGrid_GetModifiersForChunk::RunTest(const FString& Parameters)
{
	FVoxelGrid Grid;
	// Two modifiers in chunk (0,0,0)
	FVoxelModifierData ModA;
	ModA.Params.Type = EModifierType::PrimitiveSphere;
	ModA.Params.Operation = EModifierOp::Add;
	ModA.Transform = FTransform(FVector(100.f, 100.f, 100.f));
	FVoxelModifierData ModB;
	ModB.Params.Type = EModifierType::PrimitiveSphere;
	ModB.Params.Operation = EModifierOp::Add;
	ModB.Transform = FTransform(FVector(150.f, 150.f, 150.f));
	// One modifier in chunk (1,0,0) — 450cm offset
	FVoxelModifierData ModC;
	ModC.Params.Type = EModifierType::PrimitiveSphere;
	ModC.Params.Operation = EModifierOp::Add;
	ModC.Transform = FTransform(FVector(450.f, 100.f, 100.f));
	Grid.AddModifier(ModA);
	Grid.AddModifier(ModB);
	Grid.AddModifier(ModC);
	TArray<FVoxelModifierData> Mods = Grid.GetModifiersForChunk(FIntVector(0, 0, 0));
	TestEqual("Two modifiers in chunk (0,0,0)", Mods.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGrid_NoChunkWithoutModifier,
	"VoxelPlugin.Grid.NoChunkCreatedWithoutModifier",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelGrid_NoChunkWithoutModifier::RunTest(const FString& Parameters)
{
	FVoxelGrid Grid;
	TestFalse("No modifier at (0,0,0)", Grid.HasModifiersAtChunk(FIntVector(0, 0, 0)));

	FVoxelModifierData M;
	M.Params.Type = EModifierType::PrimitiveSphere;
	M.Params.Operation = EModifierOp::Add;
	M.Transform.SetLocation(FVector(200.f, 200.f, 200.f));
	M.Transform.SetScale3D(FVector(50.f));
	Grid.AddModifier(M);

	TestTrue("Modifier present at (0,0,0)", Grid.HasModifiersAtChunk(FIntVector(0, 0, 0)));
	TestFalse("No modifier at (5,5,5)", Grid.HasModifiersAtChunk(FIntVector(5, 5, 5)));
	return true;
}
