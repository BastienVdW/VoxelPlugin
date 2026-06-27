// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Engine/DataTable.h"
#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "Materials/MaterialInterface.h"
#include "Modifier/VoxelModifierTypes.h"
#include "VoxelDeveloperSettings.generated.h"

/// Fluid simulation mode for a landscape layer.
UENUM(BlueprintType)
enum class ELandscapeFluidMode : uint8
{
	Water UMETA(DisplayName = "Water (Flat & Tapered)"),
	Snow  UMETA(DisplayName = "Snow (Normal Displaced)")
};

// Fluid simulation parameters for a landscape layer.
// Embedded in FLandscapeLayerConfig; only relevant when bIsFluid = true.
USTRUCT(BlueprintType)
struct VOXELCORE_API FLandscapeLayerParams : public FTableRowBase
{
    GENERATED_BODY()

	// min height diff to trigger flow (cm)
    UPROPERTY(EditAnywhere, meta = (Units = Centimeters, ClampMin = "0.0", ClampMax = "100.0"))
	float SlopeThreshold    = 0.01f;
	
	// horizontal flow acceleration (cm/s²)
    UPROPERTY(EditAnywhere, meta = (Units = CentimetersPerSecondSquared, ClampMin = "0.0", ClampMax = "5000.0"))
	float Acceleration      = 0.1f;
	
	// max horizontal flow speed (cm/s)
    UPROPERTY(EditAnywhere, meta = (Units = CentimetersPerSecond, ClampMin = "0.0", ClampMax = "5000.0"))
	float MaxSpeed          = 1.f;
	
	// flow damping [0,1]
    UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Viscosity         = 0.8f;
	
	// fraction spilled to cave layer when border detected
    UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpillToLowerLayer = 0.1f;
	
	// mode used to generate the fluid mesh
	UPROPERTY(EditAnywhere)
	ELandscapeFluidMode FluidMode = ELandscapeFluidMode::Water;
	
	// cm the water lip drops below terrain at shorelines (~ one voxel)
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float SkirtDepth = 25.f;

	// Maximum per-step depth change considered settled (cm).
	UPROPERTY(EditAnywhere, meta = (Units = Centimeters, ClampMin = "0.0"))
	float SleepDepthThreshold = 0.001f;

	// Maximum flow speed considered settled (cm/s).
	UPROPERTY(EditAnywhere, meta = (Units = CentimetersPerSecond, ClampMin = "0.0"))
	float SleepVelocityThreshold = 0.01f;

	// Number of consecutive settled steps required before the chunk sleeps.
	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "60"))
	int32 SleepAfterStableSteps = 3;
};

// Single entry in LandscapeLayers — groups name, material, and optional fluid config.
USTRUCT()
struct VOXELCORE_API FLandscapeLayerConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, config) FName                              LayerName;
    UPROPERTY(EditAnywhere, config) TSoftObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere, config) bool                               bIsFluid = false;
    UPROPERTY(EditAnywhere, config, meta=(EditCondition="bIsFluid"))
    FLandscapeLayerParams                                              FluidParams;
};

// Project Settings → Plugins → VoxelPlugin
// All values take effect on next Play-In-Editor or game restart.
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Voxel Plugin"))
class VOXELCORE_API UVoxelDeveloperSettings : public UDeveloperSettingsBackedByCVars
{
    GENERATED_BODY()

public:
    UVoxelDeveloperSettings();

    // Voxels per chunk axis. Higher = finer shape detail but more memory and GPU cost.
    // Default: 16 (chunk = 16³ voxels = 400 cm world size at default VoxelSize).
    UPROPERTY(Config, EditAnywhere, Category="Grid", meta=(ClampMin="4", ClampMax="64"))
    int32 ChunkResolution = 16;

    // World-space size of one voxel in centimetres. Smaller = finer detail.
    // Default: 25 cm (one voxel = ≈10 inches).
    UPROPERTY(Config, EditAnywhere, Category="Grid", meta=(Units = Centimeters, ClampMin="1.0", ClampMax="1000.0"))
    float VoxelSize = 25.f;

    // Extra voxel border baked around each chunk for gradient estimation and seam stitching.
    // 1 = minimum (fine for Marching Cubes / Greedy Mesh). 2 recommended for Dual Contouring.
    // Chunk.Resolution = ChunkResolution + 2 * ChunkBorderSize.
    UPROPERTY(Config, EditAnywhere, Category="Grid", meta=(ClampMin="1", ClampMax="4"))
    int32 ChunkBorderSize = 1;

    // Resolution of the SDF grid baked from a static mesh (per axis). Higher = sharper
    // surface reconstruction but slower baking and more memory.
    // Default: 32.
    UPROPERTY(Config, EditAnywhere, Category="SDF", meta=(ClampMin="8", ClampMax="128"))
    int32 SDFBakeResolution = 32;

    // Maximum number of chunk actors kept in the pool when evicted.
    // Actors beyond this limit are destroyed instead of pooled.
    // Default: 64.
    UPROPERTY(Config, EditAnywhere, Category="Render", meta=(ClampMin="0", ClampMax="512"))
    int32 ChunkActorPoolSize = 64;

    // Algorithm used to generate the voxel mesh surface from the SDF.
    UPROPERTY(Config, EditAnywhere, Category="Render")
    EMeshAlgorithm MeshAlgorithm = EMeshAlgorithm::MarchingCubes;
	
	// Minimum world-space distance between the highest detected floor and the next lower floor in a column.
	// Used to filter out small gaps and avoid generating multiple floors in a single column.
	UPROPERTY(EditAnywhere, config, Category="Surface", meta = (Units = Centimeters, ClampMin = "0.0", ClampMax = "10000.0"))
	float SubFloorOffset = 200.0f;

    // How many chunks per axis are merged into one ProceduralMeshActor (N → N×N×N sections per actor per layer).
    UPROPERTY(EditAnywhere, config, Category="Landscape", meta = (ClampMin = "1", ClampMax = "8"))
    int32 ChunksPerProceduralMeshActor = 4;

    // Per-layer configuration: material, fluid flag, and simulation parameters.
    UPROPERTY(EditAnywhere, config, Category="Landscape")
    TArray<FLandscapeLayerConfig> LandscapeLayers;
	
public:
	const FLandscapeLayerConfig* GetLandscapeLayerConfig(FName LayerName) const
	{
		for (const FLandscapeLayerConfig& Config : LandscapeLayers)
		{
			if (Config.LayerName == LayerName)
			{
				return &Config;
			}
		}
		return nullptr;
	}
};
