// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Generation/LandscapeMeshGenerator.h"
#include "VoxelLandscapeTypes.h"
#include "VoxelSurfaceTypes.h"
#include "Settings/VoxelDeveloperSettings.h"

namespace
{
    // Builds a chunk + surface where every cell has a fixed terrain Z and water depth.
    // Returns ColRes via out-param. LayerName "Water" is registered in settings as fluid.
    static void MakeWaterFixture(float TerrainZ, float Depth, FVoxelLandscapeChunk& OutChunk,
                                 FVoxelSurfaceChunk& OutSurface, int32& OutColRes)
    {
        auto* Settings = GetMutableDefault<UVoxelDeveloperSettings>();
        Settings->ChunkResolution = 4; // small grid for tests
        Settings->LandscapeLayers.Empty();
        FLandscapeLayerConfig Cfg;
        Cfg.LayerName = TEXT("Water");
        Cfg.bIsFluid  = true;
        Cfg.FluidParams.FluidMode = ELandscapeFluidMode::Water;
        Cfg.FluidParams.SkirtDepth = 25.f;
        Settings->LandscapeLayers.Add(Cfg);

        const int32 ColRes = Settings->ChunkResolution + 2;
        OutColRes = ColRes;
        const int32 Total = ColRes * ColRes;

        FVoxelLandscapeLayer Layer;
        Layer.Cells.SetNum(Total);
        OutSurface.Columns.SetNum(Total);
        for (int32 i = 0; i < Total; ++i)
        {
            Layer.Cells[i].Depth = Depth;
            FVoxelSurfaceLevel Lvl; Lvl.WorldZ = TerrainZ;
            OutSurface.Columns[i].Levels.Add(Lvl);
        }
        OutChunk.ChunkCoord = FIntVector2(0, 0);
        OutChunk.SurfaceLayerIndex = 0;
        OutChunk.Layers.Add(TEXT("Water"), MoveTemp(Layer));
    }

    static FVector GetTriangleNormal(const FLandscapeLayerSection& Section, int32 TriStart)
    {
        const FVector& A = Section.Vertices[Section.Indices[TriStart]];
        const FVector& B = Section.Vertices[Section.Indices[TriStart + 1]];
        const FVector& C = Section.Vertices[Section.Indices[TriStart + 2]];
        return FVector::CrossProduct(B - A, C - A).GetSafeNormal();
    }

    static FVector GetRendererFacingNormal(const FLandscapeLayerSection& Section, int32 TriStart)
    {
        return -GetTriangleNormal(Section, TriStart);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_FlatSheet,
    "VoxelPlugin.Landscape.MeshGen.FlatSheet",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_FlatSheet::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
    MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/50.f, Chunk, Surface, ColRes);

    FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

    TestTrue("Has vertices", S.Vertices.Num() > 0);
    const int32 InnerRes = ColRes - 1;
    // The top surface should be flat at TerrainZ + Depth = 150 for every top vertex.
    bool bAllFlat = true;
    for (int32 i = 0; i < InnerRes * InnerRes; ++i)
    {
        if (!FMath::IsNearlyEqual(S.Vertices[i].Z, 150.f, 0.01f)) { bAllFlat = false; break; }
    }
    TestTrue("Top surface is flat at water level", bAllFlat);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_CliffStaysFlat,
    "VoxelPlugin.Landscape.MeshGen.CliffStaysFlat",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_CliffStaysFlat::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
    MakeWaterFixture(/*TerrainZ*/0.f, /*Depth*/0.f, Chunk, Surface, ColRes);

    // Build a cliff: left half terrain low (Z=0) and flooded to level 100;
    // right half terrain high (Z=300) and DRY. Shoreline runs down the middle.
    FVoxelLandscapeLayer& Layer = Chunk.Layers[TEXT("Water")];
    for (int32 cy = 0; cy < ColRes; ++cy)
    {
        for (int32 cx = 0; cx < ColRes; ++cx)
        {
            const int32 i = cx + cy * ColRes;
            const bool bLeft = cx < ColRes / 2;
            Surface.Columns[i].Levels[0].WorldZ = bLeft ? 0.f : 300.f;
            Layer.Cells[i].Depth = bLeft ? 100.f : 0.f; // wet left @ level 100, dry right
        }
    }

    FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

    // No top-surface vertex may exceed the wet water level (100). The bug placed
    // dry/edge cells up the 300-high cliff face.
    const int32 InnerRes = ColRes - 1;
    float MaxTopZ = -FLT_MAX;
    for (int32 i = 0; i < InnerRes * InnerRes; ++i)
    {
        MaxTopZ = FMath::Max(MaxTopZ, S.Vertices[i].Z);
    }
    TestTrue("Water never climbs the cliff above its level", MaxTopZ <= 100.f + 0.01f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_ShorelineHasSkirt,
    "VoxelPlugin.Landscape.MeshGen.ShorelineHasSkirt",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_ShorelineHasSkirt::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
    MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/50.f, Chunk, Surface, ColRes);

    // Carve a dry pocket in the centre so there is an interior shoreline silhouette.
    FVoxelLandscapeLayer& Layer = Chunk.Layers[TEXT("Water")];
    const int32 mid = ColRes / 2;
    Layer.Cells[mid + mid * ColRes].Depth = 0.f;

    const int32 InnerRes = ColRes - 1;
    FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

    // Skirt verts are appended after the InnerRes*InnerRes top verts, and at least one
    // must sit below terrain at TerrainZ - SkirtDepth = 100 - 25 = 75.
    bool bHasSkirt = false;
    for (int32 i = InnerRes * InnerRes; i < S.Vertices.Num(); ++i)
    {
        if (FMath::IsNearlyEqual(S.Vertices[i].Z, 75.f, 0.01f)) { bHasSkirt = true; break; }
    }
    TestTrue("Shoreline produces a downward skirt at SurfaceZ - SkirtDepth", bHasSkirt);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_SkirtUsesWetCellFloor,
    "VoxelPlugin.Landscape.MeshGen.SkirtUsesWetCellFloor",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_SkirtUsesWetCellFloor::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
    MakeWaterFixture(/*TerrainZ*/0.f, /*Depth*/0.f, Chunk, Surface, ColRes);

    FVoxelLandscapeLayer& Layer = Chunk.Layers[TEXT("Water")];
    for (int32 cy = 0; cy < ColRes; ++cy)
    {
        for (int32 cx = 0; cx < ColRes; ++cx)
        {
            const int32 i = cx + cy * ColRes;
            const bool bLeft = cx < ColRes / 2;
            Surface.Columns[i].Levels[0].WorldZ = bLeft ? 0.f : 300.f;
            Layer.Cells[i].Depth = bLeft ? 100.f : 0.f;
        }
    }

    const int32 InnerRes = ColRes - 1;
    const int32 TopVertexCount = InnerRes * InnerRes;
    FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

    bool bHasSkirt = false;
    float MaxSkirtZ = -FLT_MAX;
    for (int32 i = TopVertexCount; i < S.Vertices.Num(); ++i)
    {
        bHasSkirt = true;
        MaxSkirtZ = FMath::Max(MaxSkirtZ, S.Vertices[i].Z);
    }

    TestTrue("Cliff shoreline generated skirt vertices", bHasSkirt);
    TestTrue("Skirt vertices stay at or below the wet cap instead of climbing the dry cliff", MaxSkirtZ <= 100.f + 0.01f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_SkirtNormalsFaceOutward,
    "VoxelPlugin.Landscape.MeshGen.SkirtNormalsFaceOutward",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_SkirtNormalsFaceOutward::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
    MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/0.f, Chunk, Surface, ColRes);

    FVoxelLandscapeLayer& Layer = Chunk.Layers[TEXT("Water")];
    const int32 Mid = ColRes / 2;
    Layer.Cells[Mid + Mid * ColRes].Depth = 50.f;

    const int32 InnerRes = ColRes - 1;
    const int32 TopVertexCount = InnerRes * InnerRes;
    FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

    bool bFoundSkirtTri = false;
    bool bAllSkirtTrisUseRendererWinding = true;
    for (int32 TriStart = 0; TriStart + 2 < S.Indices.Num(); TriStart += 3)
    {
        const int32 I0 = S.Indices[TriStart];
        const int32 I1 = S.Indices[TriStart + 1];
        const int32 I2 = S.Indices[TriStart + 2];
        if (I0 < TopVertexCount || I1 < TopVertexCount || I2 < TopVertexCount)
        {
            continue;
        }

        bFoundSkirtTri = true;
        const FVector FaceNormal = GetRendererFacingNormal(S, TriStart);
        const FVector ExpectedNormal = (S.Normals[I0] + S.Normals[I1] + S.Normals[I2]).GetSafeNormal();
        if (FVector::DotProduct(FaceNormal, ExpectedNormal) < 0.99f)
        {
            bAllSkirtTrisUseRendererWinding = false;
            break;
        }
    }

    TestTrue("Fixture generated skirt triangles", bFoundSkirtTri);
    TestTrue("All skirt triangles use outward winding matching vertex normals", bAllSkirtTrisUseRendererWinding);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_TopSurfaceNormalsFaceUp,
    "VoxelPlugin.Landscape.MeshGen.TopSurfaceNormalsFaceUp",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_TopSurfaceNormalsFaceUp::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
    MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/50.f, Chunk, Surface, ColRes);

    const int32 InnerRes = ColRes - 1;
    const int32 TopVertexCount = InnerRes * InnerRes;
    FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

    bool bFoundTopTri = false;
    bool bAllTopTrisFaceUp = true;
    for (int32 TriStart = 0; TriStart + 2 < S.Indices.Num(); TriStart += 3)
    {
        const int32 I0 = S.Indices[TriStart];
        const int32 I1 = S.Indices[TriStart + 1];
        const int32 I2 = S.Indices[TriStart + 2];
        if (I0 >= TopVertexCount || I1 >= TopVertexCount || I2 >= TopVertexCount)
        {
            continue;
        }

        bFoundTopTri = true;
        const FVector FaceNormal = GetRendererFacingNormal(S, TriStart);
        if (FaceNormal.Z < 0.99f)
        {
            bAllTopTrisFaceUp = false;
            break;
        }
    }

    bool bAllTopVertexNormalsFaceUp = true;
    for (int32 i = 0; i < TopVertexCount; ++i)
    {
        if (S.Normals[i].Z < 0.99f)
        {
            bAllTopVertexNormalsFaceUp = false;
            break;
        }
    }

    TestTrue("Fixture generated top triangles", bFoundTopTri);
    TestTrue("Top triangle winding faces up", bAllTopTrisFaceUp);
    TestTrue("Top vertex normals face up", bAllTopVertexNormalsFaceUp);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_EdgeCellsKeepCapsAndOutwardSkirts,
    "VoxelPlugin.Landscape.MeshGen.EdgeCellsKeepCapsAndOutwardSkirts",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_EdgeCellsKeepCapsAndOutwardSkirts::RunTest(const FString& Parameters)
{
    const auto RunEdgeCase = [this](const TCHAR* CaseName, int32 WetX, int32 WetY, const FVector& RequiredWallNormal) -> bool
    {
        FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
        MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/0.f, Chunk, Surface, ColRes);

        FVoxelLandscapeLayer& Layer = Chunk.Layers[TEXT("Water")];
        Layer.Cells[WetX + WetY * ColRes].Depth = 50.f;

        const int32 InnerRes = ColRes - 1;
        const int32 TopVertexCount = InnerRes * InnerRes;
        FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

        int32 TopTriangleCount = 0;
        for (int32 TriStart = 0; TriStart + 2 < S.Indices.Num(); TriStart += 3)
        {
            const int32 I0 = S.Indices[TriStart];
            const int32 I1 = S.Indices[TriStart + 1];
            const int32 I2 = S.Indices[TriStart + 2];
            if (I0 < TopVertexCount && I1 < TopVertexCount && I2 < TopVertexCount)
            {
                ++TopTriangleCount;
                const FVector FaceNormal = GetRendererFacingNormal(S, TriStart);
                if (FaceNormal.Z < 0.99f)
                {
                    AddError(FString::Printf(TEXT("%s top cap winding is not upward"), CaseName));
                    return false;
                }
            }
        }

        bool bFoundRequiredWall = false;
        for (int32 TriStart = 0; TriStart + 2 < S.Indices.Num(); TriStart += 3)
        {
            const int32 I0 = S.Indices[TriStart];
            const int32 I1 = S.Indices[TriStart + 1];
            const int32 I2 = S.Indices[TriStart + 2];
            if (I0 < TopVertexCount || I1 < TopVertexCount || I2 < TopVertexCount)
            {
                continue;
            }

            const FVector FaceNormal = GetRendererFacingNormal(S, TriStart);
            if (FVector::DotProduct(FaceNormal, RequiredWallNormal) > 0.99f)
            {
                bFoundRequiredWall = true;
                break;
            }
        }

        TestEqual(FString::Printf(TEXT("%s edge wet cell emits one top cap"), CaseName), TopTriangleCount, 2);
        TestTrue(FString::Printf(TEXT("%s edge skirt includes required outward wall"), CaseName), bFoundRequiredWall);
        return TopTriangleCount == 2 && bFoundRequiredWall;
    };

    const int32 EdgeColRes = 6;
    bool bAllPassed = true;
    bAllPassed &= RunEdgeCase(TEXT("Left"), 1, 2, FVector(-1, 0, 0));
    bAllPassed &= RunEdgeCase(TEXT("Right"), EdgeColRes - 2, 2, FVector(1, 0, 0));
    bAllPassed &= RunEdgeCase(TEXT("Backward"), 2, 1, FVector(0, -1, 0));
    bAllPassed &= RunEdgeCase(TEXT("Forward"), 2, EdgeColRes - 2, FVector(0, 1, 0));
    return bAllPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_TopSurfaceMatchesWetCells,
    "VoxelPlugin.Landscape.MeshGen.TopSurfaceMatchesWetCells",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_TopSurfaceMatchesWetCells::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
    MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/0.f, Chunk, Surface, ColRes);

    FVoxelLandscapeLayer& Layer = Chunk.Layers[TEXT("Water")];
    const int32 Mid = ColRes / 2;
    Layer.Cells[Mid + Mid * ColRes].Depth = 50.f;

    const int32 InnerRes = ColRes - 1;
    const int32 TopVertexCount = InnerRes * InnerRes;
    FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

    int32 TopTriangleCount = 0;
    bool bAllTopTrianglesStayInsideWetCell = true;
    const float MinX = (Mid - 1) * 25.f;
    const float MaxX = Mid * 25.f;
    const float MinY = (Mid - 1) * 25.f;
    const float MaxY = Mid * 25.f;

    for (int32 TriStart = 0; TriStart + 2 < S.Indices.Num(); TriStart += 3)
    {
        const int32 I0 = S.Indices[TriStart];
        const int32 I1 = S.Indices[TriStart + 1];
        const int32 I2 = S.Indices[TriStart + 2];
        if (I0 >= TopVertexCount || I1 >= TopVertexCount || I2 >= TopVertexCount)
        {
            continue;
        }

        ++TopTriangleCount;
        const int32 TriIndices[3] = { I0, I1, I2 };
        for (int32 TriIndex : TriIndices)
        {
            const FVector& V = S.Vertices[TriIndex];
            if (V.X < MinX - 0.01f || V.X > MaxX + 0.01f ||
                V.Y < MinY - 0.01f || V.Y > MaxY + 0.01f)
            {
                bAllTopTrianglesStayInsideWetCell = false;
                break;
            }
        }
    }

    TestEqual("Single wet cell emits one top quad", TopTriangleCount, 2);
    TestTrue("Top surface does not extend past the wet cell boundary", bAllTopTrianglesStayInsideWetCell);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_MovedWetCellChangesTopology,
    "VoxelPlugin.Landscape.MeshGen.MovedWetCellChangesTopology",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_MovedWetCellChangesTopology::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk ChunkA; FVoxelSurfaceChunk SurfaceA; int32 ColResA = 0;
    MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/0.f, ChunkA, SurfaceA, ColResA);

    FVoxelLandscapeChunk ChunkB; FVoxelSurfaceChunk SurfaceB; int32 ColResB = 0;
    MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/0.f, ChunkB, SurfaceB, ColResB);

    FVoxelLandscapeLayer& LayerA = ChunkA.Layers[TEXT("Water")];
    FVoxelLandscapeLayer& LayerB = ChunkB.Layers[TEXT("Water")];
    const int32 Mid = ColResA / 2;
    LayerA.Cells[Mid + Mid * ColResA].Depth = 50.f;
    LayerB.Cells[(Mid + 1) + Mid * ColResB].Depth = 50.f;

    const FLandscapeLayerSection A = FLandscapeMeshGenerator::ComputeSection(ChunkA, SurfaceA, TEXT("Water"), 25.f);
    const FLandscapeLayerSection B = FLandscapeMeshGenerator::ComputeSection(ChunkB, SurfaceB, TEXT("Water"), 25.f);

    TestEqual("Moved single wet cell keeps the same vertex count", A.Vertices.Num(), B.Vertices.Num());
    TestEqual("Moved single wet cell keeps the same index count", A.Indices.Num(), B.Indices.Num());

    bool bIndicesChanged = false;
    const int32 CompareCount = FMath::Min(A.Indices.Num(), B.Indices.Num());
    for (int32 i = 0; i < CompareCount; ++i)
    {
        if (A.Indices[i] != B.Indices[i])
        {
            bIndicesChanged = true;
            break;
        }
    }

    TestTrue("Moved single wet cell changes index topology", bIndicesChanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMesh_MissingSurfaceDoesNotRenderFluid,
    "VoxelPlugin.Landscape.MeshGen.MissingSurfaceDoesNotRenderFluid",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLandscapeMesh_MissingSurfaceDoesNotRenderFluid::RunTest(const FString& Parameters)
{
    FVoxelLandscapeChunk Chunk; FVoxelSurfaceChunk Surface; int32 ColRes = 0;
    MakeWaterFixture(/*TerrainZ*/100.f, /*Depth*/0.f, Chunk, Surface, ColRes);

    FVoxelLandscapeLayer& Layer = Chunk.Layers[TEXT("Water")];
    const int32 Mid = ColRes / 2;
    const int32 WetIdx = Mid + Mid * ColRes;
    Layer.Cells[WetIdx].Depth = 50.f;
    Surface.Columns[WetIdx].Levels.Reset();

    FLandscapeLayerSection S = FLandscapeMeshGenerator::ComputeSection(Chunk, Surface, TEXT("Water"), 25.f);

    TestEqual("Wet cell without matching surface emits no triangles", S.Indices.Num(), 0);
    return true;
}
