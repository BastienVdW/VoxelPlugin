// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "VoxelModifierTypes.generated.h"

UENUM()
enum class EModifierType : uint8
{
    PrimitiveSphere,
    PrimitiveBox,
    MeshSDF,    // future: signed distance field baked from static mesh
    Volume,     // future: 3D density volume texture
};

UENUM()
enum class EModifierOp : uint8
{
    Add,    // fill (union)
    Remove, // carve (subtract)
};

// Opaque handle returned by FVoxelGrid::AddModifier. Invalidated after RemoveModifier.
USTRUCT()
struct VOXELCORE_API FModifierHandle
{
	GENERATED_BODY()
	
	UPROPERTY()
    uint32 Id = 0;
	
    bool IsValid() const { return Id != 0; }
	void Invalidate() { Id = 0; }
    static FModifierHandle Invalid() { return FModifierHandle{}; }
    bool operator==(const FModifierHandle& O) const { return Id == O.Id; }
    bool operator!=(const FModifierHandle& O) const { return Id != O.Id; }
    friend uint32 GetTypeHash(const FModifierHandle& H) { return H.Id; }
};

UENUM()
enum class EMeshAlgorithm : uint8
{
    MarchingCubes,   // smooth or semi-blocky surface; post-processed by Laplacian smoothing
    GreedyMesh,      // perfectly cubic quads; ignores MeshSmoothing
    DualContouring,  // QEF-based sharp-feature preservation
};

// User-facing parameters for creating a modifier. Passed to Voxel::Modifier::Utils::AddModifier.
USTRUCT(BlueprintType)
struct VOXELCORE_API FVoxelModifierParameters
{
    GENERATED_BODY()

	bool operator==(const FVoxelModifierParameters& Other) const
	{
		return Type == Other.Type &&
			Operation == Other.Operation &&
			SurfaceType == Other.SurfaceType &&
			MeshSmoothing == Other.MeshSmoothing;
	}

	bool operator!=(const FVoxelModifierParameters& Other) const { return !(*this == Other); }

    UPROPERTY(EditAnywhere, Category="Voxel")
    EModifierType Type = EModifierType::MeshSDF;

    UPROPERTY(EditAnywhere, Category="Voxel")
    EModifierOp Operation = EModifierOp::Add;

    UPROPERTY(EditAnywhere, Category="Voxel")
    uint8 SurfaceType = 0;

    UPROPERTY(EditAnywhere, Category="Voxel Render", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MeshSmoothing = 0.f;
};

// Baked SDF data for a MeshSDF modifier. Populated by Voxel::Modifier::Utils::AddModifier.
USTRUCT()
struct VOXELCORE_API FVoxelSDFData
{
    GENERATED_BODY()

	bool operator==(const FVoxelSDFData& Other) const
	{
		return Samples == Other.Samples &&
			UVSamples == Other.UVSamples &&
			Resolution == Other.Resolution &&
			LocalBounds == Other.LocalBounds;
	}

	bool operator!=(const FVoxelSDFData& Other) const { return !(*this == Other); }

    // Flat Resolution^3 array of signed distances (cm)
    UPROPERTY()
    TArray<float> Samples;

    // UV samples parallel to Samples — populated only when source mesh has UV channel 0
    UPROPERTY()
    TArray<FVector2f> UVSamples;

    UPROPERTY()
    int32 Resolution = 0;

    UPROPERTY()
    FBox LocalBounds = FBox(EForceInit::ForceInit);
};

// Pure data describing one voxel modifier. No UObject references.
USTRUCT()
struct VOXELCORE_API FVoxelModifierData
{
    GENERATED_BODY()

	bool operator==(const FVoxelModifierData& Other) const
	{
		return Params == Other.Params &&
			Transform.Equals(Other.Transform) &&
			SDF == Other.SDF &&
			MaterialPath == Other.MaterialPath;
	}

	bool operator!=(const FVoxelModifierData& Other) const { return !(*this == Other); }

    UPROPERTY()
    FVoxelModifierParameters Params;

    // PrimitiveSphere: radius in cm stored in Transform.Scale.X
    // PrimitiveBox: half-extents in cm stored in Transform.Scale
    UPROPERTY()
    FTransform Transform = FTransform::Identity;

    // Populated for MeshSDF type only
    UPROPERTY()
    FVoxelSDFData SDF;

    // Soft path to the UMaterialInterface applied to this modifier's voxels.
    // Resolved via FVoxelMaterialRegistry at bake time; no hard engine reference here.
    UPROPERTY()
    FSoftObjectPath MaterialPath;

    FBoxSphereBounds GetWorldBounds() const;
};
