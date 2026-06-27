// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once
#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"

struct VOXELCORE_API FVoxelMeshData
{
    TArray<FVector3f> Vertices;
    TArray<int32>     Triangles;
    TArray<uint16>    TriangleMaterialHashes;  // parallel to triangles (one per tri); FVoxelMaterialRegistry key
    TArray<FVector2f> UVs;                     // per vertex, parallel to Vertices
};

class VOXELCORE_API FVoxelMarchingCubes
{
public:
    // Smoothness: 0.0 = snap to midpoint (blocky), 1.0 = standard linear interpolation (smooth)
    static FVoxelMeshData ExtractSurface(const FVoxelChunk& Chunk, int32 LODStep = 1, float Smoothness = 0.0f);

private:
    static void ProcessCube(const FVoxelChunk& Chunk, int32 X, int32 Y, int32 Z, int32 LODStep,
                             float Smoothness, FVoxelMeshData& OutMesh);
    static FVector3f InterpolateEdge(FVector3f P1, float D1, FVector3f P2, float D2, float Smoothness);
    static FVector2f InterpolateUV   (FVector2f  U1, float D1, FVector2f  U2, float D2, float Smoothness);

    // Moves each vertex toward the average position of its edge-connected neighbours.
    // Iterations = 0 → no change. Vertices only; UVs and material hashes are unchanged.
    static FVoxelMeshData ApplyLaplacianSmoothing(FVoxelMeshData Mesh, int32 Iterations);

    // Standard marching cubes lookup tables (256-entry edge table + 256x16 tri table)
    // Source: Paul Bourke — http://paulbourke.net/geometry/polygonise/ (public domain)
    static const int32 EdgeTable[256];
    static const int32 TriTable[256][16];
};
