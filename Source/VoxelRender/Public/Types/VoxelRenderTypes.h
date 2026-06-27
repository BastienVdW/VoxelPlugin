#pragma once
#include "CoreMinimal.h"
#include "Voxel/VoxelTypes.h"
#include "Modifier/VoxelModifierTypes.h"

struct FVoxelRenderEvent
{
    FIntVector     ChunkCoord;
    FVector        ChunkOriginWorld = FVector::ZeroVector;
    int32          Resolution       = 0;   // canonical voxel count (excludes border)
    int32          BorderSize       = 1;   // extra voxel ring; Voxels.Num() == (Resolution + 2*BorderSize)^3
    float          VoxelSize        = 0.f;
    float          MeshSmoothing    = 0.0f;
    TArray<FVoxel> Voxels;                 // chunk's full baked grid including border
};

struct FVoxelRenderMeshSection
{
    TArray<FVector>   Vertices;
    TArray<int32>     Triangles;
    TArray<FVector>   Normals;
    TArray<FVector2D> UV0;
};

struct FVoxelRenderMeshResult
{
    FIntVector                            ChunkCoord;
    FVector                               ChunkOriginWorld = FVector::ZeroVector;
    TMap<uint16, FVoxelRenderMeshSection> Sections; // keyed by FVoxelMaterialRegistry hash
};
