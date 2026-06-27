// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "System/VoxelSurfaceSubsystem.h"
#include "Generation/VoxelSurfaceGenerator.h"
#include "Grid/VoxelGrid.h"
#include "Modifier/VoxelModifierTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurface_Subsystem_GeneratesChunks,
    "VoxelPlugin.Surface.Subsystem.GeneratesChunks",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelSurface_Subsystem_GeneratesChunks::RunTest(const FString& Parameters)
{
    // Arrange: grid with a sphere modifier at chunk (0,0,0)
    FVoxelGrid Grid;
    FVoxelModifierData Modifier;
    Modifier.Params.Type      = EModifierType::PrimitiveSphere;
    Modifier.Params.Operation = EModifierOp::Add;
    Modifier.Transform.SetLocation(FVector(200.f, 200.f, 200.f));
    Modifier.Transform.SetScale3D(FVector(150.f));
    Grid.AddModifier(Modifier);
    Grid.GetOrCreateChunk(FIntVector(0, 0, 0));

    // Act: direct generator usage (subsystem is a WorldSubsystem; test without a World)
    FVoxelSurfaceGenerator Generator;
    TMap<FIntVector2, FVoxelSurfaceChunk> Chunks;
    TArray<FIntVector2> DirtyXY = { FIntVector2(0, 0) };
    Chunks.Add(FIntVector2(0, 0), FVoxelSurfaceChunk{});
    Chunks[FIntVector2(0,0)].ChunkCoord = FIntVector2(0, 0);

    Generator.StartGeneration(Grid, DirtyXY, Chunks);
    Generator.ForceEndGeneration();

    // Assert
    const FVoxelSurfaceChunk* Chunk = Chunks.Find(FIntVector2(0, 0));
    TestNotNull("Chunk (0,0) exists in map", Chunk);
    if (Chunk)
    {
        TestFalse("Columns array populated", Chunk->Columns.IsEmpty());
        // Find any column with at least one level (sphere covers center)
        bool bFoundLevel = false;
        for (const FVoxelSurfaceColumn& Col : Chunk->Columns)
        {
            if (!Col.Levels.IsEmpty()) { bFoundLevel = true; break; }
        }
        TestTrue("At least one column has a surface level", bFoundLevel);
    }

    return true;
}
