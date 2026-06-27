// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Generation/LandscapeMeshGenerator.h"

#include "Settings/VoxelDeveloperSettings.h"

/*static*/ bool FLandscapeMeshGenerator::IsRenderableCell(const TArray<bool>& CellWet, int32 ColRes, int32 CellX, int32 CellY)
{
    CellX = FMath::Clamp(CellX, 0, ColRes - 1);
    CellY = FMath::Clamp(CellY, 0, ColRes - 1);
    return CellWet[CellX + CellY * ColRes];
}

/*static*/ float FLandscapeMeshGenerator::ComputeTopVertexZ(
    const TArray<float>& CellSurfZ,
    const TArray<float>& CellLevel,
    const TArray<bool>& CellWet,
    int32 ColRes,
    int32 VertexX,
    int32 VertexY,
    bool bHasWetCell,
    float MinWetLevel)
{
    float Sum = 0.f;
    int32 Count = 0;
    for (int32 OffsetY = 0; OffsetY <= 1; ++OffsetY)
    {
        for (int32 OffsetX = 0; OffsetX <= 1; ++OffsetX)
        {
            const int32 SampleX = FMath::Clamp(VertexX + OffsetX, 0, ColRes - 1);
            const int32 SampleY = FMath::Clamp(VertexY + OffsetY, 0, ColRes - 1);
            const int32 SampleIdx = SampleX + SampleY * ColRes;
            if (CellWet[SampleIdx])
            {
                Sum += CellLevel[SampleIdx];
                ++Count;
            }
        }
    }

    if (Count > 0)
    {
        return Sum / Count;
    }

    const int32 CellIdx = FMath::Clamp(VertexX + 1, 0, ColRes - 1) + FMath::Clamp(VertexY + 1, 0, ColRes - 1) * ColRes;
    return bHasWetCell ? MinWetLevel : CellSurfZ[CellIdx];
}

/*static*/ float FLandscapeMeshGenerator::ComputeSkirtFloorZ(
    const TArray<float>& CellSurfZ,
    int32 ColRes,
    int32 VertexX,
    int32 VertexY,
    float SkirtDepth)
{
    // Offset vertex coords by +1 to get the cell that "owns" this corner,
    // matching the original per-vertex terrain sampling before refactor.
    const int32 CellX = FMath::Clamp(VertexX + 1, 0, ColRes - 1);
    const int32 CellY = FMath::Clamp(VertexY + 1, 0, ColRes - 1);
    return CellSurfZ[CellX + CellY * ColRes] - SkirtDepth;
}

/*static*/ FLandscapeLayerSection FLandscapeMeshGenerator::ComputeSection(
    const FVoxelLandscapeChunk& Chunk,
    const FVoxelSurfaceChunk&   Surface,
    FName                        LayerName,
    float                        VoxelSize,
    FVector2D                    LocalOffset)
{
    FLandscapeLayerSection Out;

    const FVoxelLandscapeLayer* Layer = Chunk.Layers.Find(LayerName);
    if (!Layer) return Out;
    const TArray<FLandscapeCell>& LayerCells = Layer->Cells;

    const auto* Settings = GetDefault<UVoxelDeveloperSettings>();
	const FLandscapeLayerConfig* LayerConfig = Settings->GetLandscapeLayerConfig(LayerName);
    const int32 ColRes = Settings->ChunkResolution + 2;
    const int32 TotalCells = ColRes * ColRes;

    if (LayerCells.Num() != TotalCells || Surface.Columns.Num() != TotalCells) return Out;

    const int32 InnerRes  = ColRes - 1; 
    const float ChunkSize = Settings->ChunkResolution * VoxelSize;
    const float OriginX   = Chunk.ChunkCoord.X * ChunkSize;
    const float OriginY   = Chunk.ChunkCoord.Y * ChunkSize;
    
    const int32 TotalVertices = InnerRes * InnerRes;
    const int32 SurfaceLayerIdx = Chunk.SurfaceLayerIndex;
    const float InverseChunkSize = (ChunkSize > 0.0f) ? (1.0f / ChunkSize) : 0.0f;

    // 1. Bulk allocation
    Out.Vertices.SetNumUninitialized(TotalVertices);
    Out.UVs.SetNumUninitialized(TotalVertices);
    Out.Normals.SetNumUninitialized(TotalVertices); // We will compute these properly later

    // 2a. Precompute per-cell terrain Z, wetness, and water level over the full grid.
    TArray<float> CellSurfZ;  CellSurfZ.SetNumUninitialized(TotalCells);
    TArray<float> CellLevel;  CellLevel.SetNumUninitialized(TotalCells); // SurfZ + Depth
    TArray<bool>  CellWet;    CellWet.SetNumUninitialized(TotalCells);
    bool bHasWetCell = false;
    float MinWetLevel = FLT_MAX;
    for (int32 i = 0; i < TotalCells; ++i)
    {
        float SZ = 0.f;
        const FVoxelSurfaceColumn& Col = Surface.Columns[i];
        const bool bHasSurface = Col.Levels.IsValidIndex(SurfaceLayerIdx);
        if (bHasSurface)
        {
            SZ = Col.Levels[SurfaceLayerIdx].WorldZ;
        }
        const float V = LayerCells[i].Depth;
        CellSurfZ[i] = SZ;
        CellWet[i]   = bHasSurface && V > 0.f;
        CellLevel[i] = SZ + V;
        if (CellWet[i])
        {
            bHasWetCell = true;
            MinWetLevel = FMath::Min(MinWetLevel, CellLevel[i]);
        }
    }

    // 2b. Vertex & UV generation: flat water level, with dry shoreline vertices
    // following adjacent wet levels instead of climbing dry cliff terrain.
    for (int32 icy = 0; icy < InnerRes; ++icy)
    {
        const float TargetY = OriginY + (icy * VoxelSize);
        const float UvY = TargetY * InverseChunkSize;
        const int32 RowOffset = icy * InnerRes;

        for (int32 icx = 0; icx < InnerRes; ++icx)
        {
            const int32 VertIdx = icx + RowOffset;
            const float TargetX = OriginX + (icx * VoxelSize);

            const float VertZ = ComputeTopVertexZ(CellSurfZ, CellLevel, CellWet, ColRes, icx, icy, bHasWetCell, MinWetLevel);

            Out.Vertices[VertIdx] = FVector(TargetX - LocalOffset.X, TargetY - LocalOffset.Y, VertZ);
            Out.UVs[VertIdx] = FVector2D(TargetX * InverseChunkSize, UvY);
        }
    }

    // 3. Normal Generation Pass (Crucial for Water Shading)
    // We do this AFTER vertices are positioned so the math reacts to the new taper
    for (int32 icy = 0; icy < InnerRes; ++icy)
    {
        const int32 RowOffset = icy * InnerRes;
        for (int32 icx = 0; icx < InnerRes; ++icx)
        {
            const int32 VertIdx = icx + RowOffset;

            // Safely clamp neighbor lookups to grid edges
            int32 L = (icx > 0) ? VertIdx - 1 : VertIdx;
            int32 R = (icx < InnerRes - 1) ? VertIdx + 1 : VertIdx;
            int32 D = (icy > 0) ? VertIdx - InnerRes : VertIdx;
            int32 U = (icy < InnerRes - 1) ? VertIdx + InnerRes : VertIdx;

            // Compute the normal from the generated vertex heights
            FVector TangentX = Out.Vertices[R] - Out.Vertices[L];
            FVector TangentY = Out.Vertices[U] - Out.Vertices[D];
            
            // Cross product gives us the true surface normal
            Out.Normals[VertIdx] = FVector::CrossProduct(TangentX, TangentY).GetSafeNormal();
        }
    }

    // 4. Index/Quad Generation Pass
    const int32 MaxQuads = (InnerRes - 1) * (InnerRes - 1);
    Out.Indices.Reserve(MaxQuads * 6);

    for (int32 icy = 0; icy < InnerRes - 1; ++icy)
    {
        const int32 CellRowOffset = (icy + 1) * ColRes;
        const int32 VertRowOffset = icy * InnerRes;

        for (int32 icx = 0; icx < InnerRes - 1; ++icx)
        {
            const int32 CellIdx = (icx + 1) + CellRowOffset;

            // Each wet cell owns exactly one surface quad. This keeps the top
            // surface boundary aligned with the skirt silhouette.
            if (!CellWet[CellIdx])
            {
                continue;
            }

            const int32 V00 = icx + VertRowOffset;
            const int32 V10 = V00 + 1;
            const int32 V01 = V00 + InnerRes;
            const int32 V11 = V01 + 1;

            Out.Indices.Add(V00);
            Out.Indices.Add(V11);
            Out.Indices.Add(V10);
            Out.Indices.Add(V00);
            Out.Indices.Add(V01);
            Out.Indices.Add(V11);
        }
    }

    // 5. Skirt / lip generation: vertical walls along the water silhouette.
    const float SkirtDepth = LayerConfig ? LayerConfig->FluidParams.SkirtDepth : 0.f;
    if (SkirtDepth > 0.f)
    {
        auto IsWetCell = [&](int32 cx, int32 cy) -> bool
        {
            return IsRenderableCell(CellWet, ColRes, cx, cy);
        };

        // Emit a wall between two adjacent top vertices, facing outward.
        // OutDir is the horizontal outward normal.
        auto AddWall = [&](int32 vx0, int32 vy0, int32 vx1, int32 vy1, int32 WetCellX, int32 WetCellY, const FVector& OutDir)
        {
            const int32 Top0 = vx0 + vy0 * InnerRes;
            const int32 Top1 = vx1 + vy1 * InnerRes;
            const FVector P0 = Out.Vertices[Top0];
            const FVector P1 = Out.Vertices[Top1];
            const float FloorZ = CellSurfZ[WetCellX + WetCellY * ColRes] - SkirtDepth;
            const float Floor0 = FloorZ;
            const float Floor1 = FloorZ;
            const FVector B0(P0.X, P0.Y, Floor0);
            const FVector B1(P1.X, P1.Y, Floor1);

            const int32 Base = Out.Vertices.Num();
            Out.Vertices.Add(P0); Out.Vertices.Add(P1); Out.Vertices.Add(B1); Out.Vertices.Add(B0);
            for (int32 k = 0; k < 4; ++k)
            {
                Out.Normals.Add(OutDir);
                Out.UVs.Add(FVector2D(0.f, 0.f));
            }
            const FVector Segment = P1 - P0;
            const FVector Down = B0 - P0;
            const FVector CandidateNormal = FVector::CrossProduct(Segment, Down).GetSafeNormal();
            if (FVector::DotProduct(CandidateNormal, OutDir) <= 0.f)
            {
                Out.Indices.Add(Base + 0); Out.Indices.Add(Base + 1); Out.Indices.Add(Base + 2);
                Out.Indices.Add(Base + 0); Out.Indices.Add(Base + 2); Out.Indices.Add(Base + 3);
            }
            else
            {
                Out.Indices.Add(Base + 0); Out.Indices.Add(Base + 3); Out.Indices.Add(Base + 1);
                Out.Indices.Add(Base + 1); Out.Indices.Add(Base + 3); Out.Indices.Add(Base + 2);
            }
        };

        for (int32 cy = 1; cy < ColRes - 1; ++cy)
        {
            for (int32 cx = 1; cx < ColRes - 1; ++cx)
            {
                if (!IsWetCell(cx, cy)) continue;

                const int32 vx0 = cx - 1;
                const int32 vx1 = cx;
                const int32 vy0 = cy - 1;
                const int32 vy1 = cy;

                if (!IsWetCell(cx - 1, cy)) AddWall(vx0, vy0, vx0, vy1, cx, cy, FVector(-1, 0, 0));
                if (!IsWetCell(cx + 1, cy)) AddWall(vx1, vy0, vx1, vy1, cx, cy, FVector(1, 0, 0));
                if (!IsWetCell(cx, cy - 1)) AddWall(vx0, vy0, vx1, vy0, cx, cy, FVector(0, -1, 0));
                if (!IsWetCell(cx, cy + 1)) AddWall(vx0, vy1, vx1, vy1, cx, cy, FVector(0, 1, 0));
            }
        }
    }

    return Out;
}
