// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Grid/VoxelModifierGrid.h"
#include "Modifier/VoxelModifierTypes.h"

// Helper: sphere modifier centered at (X,Y,Z) with radius R
static FVoxelModifierData MakeSphere(float X, float Y, float Z, float R)
{
    FVoxelModifierData M;
    M.Params.Type      = EModifierType::PrimitiveSphere;
    M.Params.Operation = EModifierOp::Add;
    M.Transform.SetLocation(FVector(X, Y, Z));
    M.Transform.SetScale3D(FVector(R));
    return M;
}

// Default grid: 16 voxels * 25cm = 400cm per chunk
static constexpr float ChunkSize   = 400.f;
static constexpr float BorderSize  = 25.f; // 1 voxel border

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelModifierGrid_HasModifiers_AfterAdd,
    "VoxelPlugin.ModifierGrid.HasModifiers_AfterAdd",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVoxelModifierGrid_HasModifiers_AfterAdd::RunTest(const FString&)
{
    FVoxelModifierGrid Grid;
    // Sphere at (200,200,200) r=50 — sits inside chunk (0,0,0)
    FVoxelModifierData M = MakeSphere(200.f, 200.f, 200.f, 50.f);
    Grid.Add(1, M);
    TestTrue("Chunk (0,0,0) has modifier", Grid.HasModifiersAtChunk(FIntVector(0,0,0), ChunkSize));
    TestFalse("Chunk (1,0,0) has no modifier", Grid.HasModifiersAtChunk(FIntVector(1,0,0), ChunkSize));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelModifierGrid_HasModifiers_AfterRemove,
    "VoxelPlugin.ModifierGrid.HasModifiers_AfterRemove",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVoxelModifierGrid_HasModifiers_AfterRemove::RunTest(const FString&)
{
    FVoxelModifierGrid Grid;
    FVoxelModifierData M = MakeSphere(200.f, 200.f, 200.f, 50.f);
    Grid.Add(1, M);
    TestTrue("Present before remove", Grid.HasModifiersAtChunk(FIntVector(0,0,0), ChunkSize));
    Grid.Remove(1, M);
    TestFalse("Absent after remove", Grid.HasModifiersAtChunk(FIntVector(0,0,0), ChunkSize));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelModifierGrid_GetModifiersForChunk_Correct,
    "VoxelPlugin.ModifierGrid.GetModifiersForChunk_Correct",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVoxelModifierGrid_GetModifiersForChunk_Correct::RunTest(const FString&)
{
    FVoxelModifierGrid Grid;
    // Two in chunk (0,0,0), one in chunk (1,0,0)
    Grid.Add(1, MakeSphere(100.f, 100.f, 100.f, 50.f));
    Grid.Add(2, MakeSphere(200.f, 200.f, 200.f, 50.f));
    Grid.Add(3, MakeSphere(600.f, 100.f, 100.f, 50.f)); // chunk (1,0,0)

    TArray<FVoxelModifierData> Chunk0 = Grid.GetModifiersForChunk(FIntVector(0,0,0), ChunkSize, BorderSize);
    TArray<FVoxelModifierData> Chunk1 = Grid.GetModifiersForChunk(FIntVector(1,0,0), ChunkSize, BorderSize);

    TestEqual("Chunk (0,0,0) has 2 modifiers", Chunk0.Num(), 2);
    TestEqual("Chunk (1,0,0) has 1 modifier",  Chunk1.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelModifierGrid_GetById,
    "VoxelPlugin.ModifierGrid.GetById",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVoxelModifierGrid_GetById::RunTest(const FString&)
{
    FVoxelModifierGrid Grid;
    FVoxelModifierData M = MakeSphere(100.f, 100.f, 100.f, 50.f);
    M.Params.SurfaceType = 7;
    Grid.Add(42, M);
    const FVoxelModifierData* Found = Grid.GetById(42);
    TestNotNull("Found by ID", Found);
    TestEqual("SurfaceType matches", Found ? Found->Params.SurfaceType : 0, (uint8)7);
    TestNull("Missing ID returns null", Grid.GetById(99));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelModifierGrid_LargeModifier_SpillList,
    "VoxelPlugin.ModifierGrid.LargeModifier_SpillList",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVoxelModifierGrid_LargeModifier_SpillList::RunTest(const FString&)
{
    FVoxelModifierGrid Grid;
    // Radius = 40000cm (400m) — exceeds the ~32000cm coarsest grid level, will spill
    FVoxelModifierData M = MakeSphere(0.f, 0.f, 0.f, 40000.f);
    Grid.Add(1, M);
    TestTrue("Large modifier visible via HasModifiersAtChunk", Grid.HasModifiersAtChunk(FIntVector(0,0,0), 400.f));
    TArray<FVoxelModifierData> Mods = Grid.GetModifiersForChunk(FIntVector(0,0,0), 400.f, 25.f);
    TestEqual("Large modifier returned by GetModifiersForChunk", Mods.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelModifierGrid_QueryResultsSortedByModifierId,
    "VoxelPlugin.ModifierGrid.QueryResultsSortedByModifierId",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVoxelModifierGrid_QueryResultsSortedByModifierId::RunTest(const FString&)
{
    FVoxelModifierGrid Grid;

    FVoxelModifierData M30 = MakeSphere(100.f, 100.f, 100.f, 50.f);
    FVoxelModifierData M10 = MakeSphere(120.f, 100.f, 100.f, 50.f);
    FVoxelModifierData M20 = MakeSphere(140.f, 100.f, 100.f, 50.f);
    M30.Params.SurfaceType = 30;
    M10.Params.SurfaceType = 10;
    M20.Params.SurfaceType = 20;

    Grid.Add(30, M30);
    Grid.Add(10, M10);
    Grid.Add(20, M20);

    const TArray<FVoxelModifierData> ChunkMods =
        Grid.GetModifiersForChunk(FIntVector(0, 0, 0), ChunkSize, BorderSize);
    TestEqual("Chunk query count", ChunkMods.Num(), 3);
    if (ChunkMods.Num() == 3)
    {
        TestEqual("Chunk query item 0", ChunkMods[0].Params.SurfaceType, static_cast<uint8>(10));
        TestEqual("Chunk query item 1", ChunkMods[1].Params.SurfaceType, static_cast<uint8>(20));
        TestEqual("Chunk query item 2", ChunkMods[2].Params.SurfaceType, static_cast<uint8>(30));
    }

    const TArray<FVoxelModifierData> XYMods =
        Grid.GetModifiersAtXY(FIntVector2(0, 0), ChunkSize, BorderSize);
    TestEqual("XY query count", XYMods.Num(), 3);
    if (XYMods.Num() == 3)
    {
        TestEqual("XY query item 0", XYMods[0].Params.SurfaceType, static_cast<uint8>(10));
        TestEqual("XY query item 1", XYMods[1].Params.SurfaceType, static_cast<uint8>(20));
        TestEqual("XY query item 2", XYMods[2].Params.SurfaceType, static_cast<uint8>(30));
    }

    const TArray<FModifierHandle> Handles = Grid.GetModifierHandlesInBounds(
        FBox(FVector(0.f), FVector(ChunkSize)));
    TestEqual("Handle query count", Handles.Num(), 3);
    if (Handles.Num() == 3)
    {
        TestEqual("Handle query item 0", Handles[0].Id, static_cast<uint32>(10));
        TestEqual("Handle query item 1", Handles[1].Id, static_cast<uint32>(20));
        TestEqual("Handle query item 2", Handles[2].Id, static_cast<uint32>(30));
    }

    return true;
}
