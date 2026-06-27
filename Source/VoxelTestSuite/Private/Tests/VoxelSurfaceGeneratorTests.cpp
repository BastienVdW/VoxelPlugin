// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Generation/VoxelSurfaceGenerator.h"
#include "Modifier/VoxelModifierTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurface_ScanColumn_SphereSingleFloor,
    "VoxelPlugin.Surface.ScanColumn.SphereSingleFloor",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelSurface_ScanColumn_SphereSingleFloor::RunTest(const FString& Parameters)
{
    // Sphere at (0,0,0), radius 200cm. Column at (0,0) should find floor near Z=200.
    FVoxelModifierData Modifier;
    Modifier.Params.Type      = EModifierType::PrimitiveSphere;
    Modifier.Params.Operation = EModifierOp::Add;
    Modifier.Transform.SetLocation(FVector(0.f, 0.f, 0.f));
    Modifier.Transform.SetScale3D(FVector(200.f));

    TArray<FVoxelModifierData> Modifiers = { Modifier };

    FVoxelSurfaceColumn Col = FVoxelSurfaceGenerator::ScanColumn(
        0.f, 0.f,
        /*MinWorldZ=*/ -600.f, /*MaxWorldZ=*/ 600.f,
        /*StepSize=*/  12.5f,
        /*SubFloorOffset=*/ 0.f,
        Modifiers);

    TestEqual("Exactly one floor level found", Col.Levels.Num(), 1);
    if (Col.Levels.Num() == 1)
    {
        TestTrue("Floor near top of sphere (Z ≈ 200)",
            FMath::Abs(Col.Levels[0].WorldZ - 200.f) < 15.f);
        TestEqual("Floor is LayerIndex 0", Col.Levels[0].LayerIndex, 0);
        TestTrue("Normal points roughly up", Col.Levels[0].Normal.Z > 0.7f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurface_ScanColumn_NoModifiers,
    "VoxelPlugin.Surface.ScanColumn.NoModifiers",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelSurface_ScanColumn_NoModifiers::RunTest(const FString& Parameters)
{
    TArray<FVoxelModifierData> Modifiers;
    FVoxelSurfaceColumn Col = FVoxelSurfaceGenerator::ScanColumn(
        0.f, 0.f, -400.f, 400.f, 12.5f, 0.f, Modifiers);
    TestTrue("No modifiers → no floor levels", Col.Levels.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSurface_ScanColumn_CaveSubFloor,
    "VoxelPlugin.Surface.ScanColumn.CaveSubFloor",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelSurface_ScanColumn_CaveSubFloor::RunTest(const FString& Parameters)
{
    // Top sphere at Z=300 (radius 100): floor ~Z=400
    // Bottom sphere at Z=-200 (radius 100): floor ~Z=-100
    // With SubFloorOffset=300 we should find both floors.
    FVoxelModifierData Top, Bottom;
    Top.Params.Type = Bottom.Params.Type = EModifierType::PrimitiveSphere;
    Top.Params.Operation = Bottom.Params.Operation = EModifierOp::Add;
    Top.Transform.SetLocation(FVector(0.f, 0.f, 300.f));
    Top.Transform.SetScale3D(FVector(100.f));
    Bottom.Transform.SetLocation(FVector(0.f, 0.f, -200.f));
    Bottom.Transform.SetScale3D(FVector(100.f));

    TArray<FVoxelModifierData> Modifiers = { Top, Bottom };

    FVoxelSurfaceColumn Col = FVoxelSurfaceGenerator::ScanColumn(
        0.f, 0.f, -600.f, 600.f, 12.5f, /*SubFloorOffset=*/300.f, Modifiers);

    TestEqual("Two floor levels found", Col.Levels.Num(), 2);
    if (Col.Levels.Num() == 2)
    {
        TestTrue("Level 0 is higher than Level 1", Col.Levels[0].WorldZ > Col.Levels[1].WorldZ);
        TestEqual("Level 0 is LayerIndex 0", Col.Levels[0].LayerIndex, 0);
        TestEqual("Level 1 is LayerIndex 1", Col.Levels[1].LayerIndex, 1);
    }
    return true;
}
