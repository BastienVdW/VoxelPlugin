// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelLandscapeTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_TypeDefaults,
    "VoxelPlugin.Landscape.TypeDefaults",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_TypeDefaults::RunTest(const FString& Parameters)
{
    FLandscapeCell Cell;
    TestEqual("Cell default value is 0", Cell.Depth, 0.f);

    FVoxelLandscapeChunk Chunk;
    Chunk.ChunkCoord       = FIntVector2(1, 2);
    Chunk.SurfaceLayerIndex = 0;
    TestFalse("New chunk is not dirty", Chunk.bDirty);
    TestTrue("Layers map is empty", Chunk.Layers.IsEmpty());

    FLandscapeLayerParams Params;
    // TestFalse("Not simulated by default", Params.bSimulated);
    TestTrue("SlopeThreshold > 0", Params.SlopeThreshold > 0.f);

    TestEqual("SkirtDepth default is 25cm", Params.SkirtDepth, 25.f);

    return true;
}
