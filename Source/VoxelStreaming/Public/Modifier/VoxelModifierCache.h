// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "Modifier/VoxelModifierTypes.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "VoxelModifierCache.generated.h"

USTRUCT()
struct VOXELSTREAMING_API FVoxelMeshComponentIdentifier
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	FSoftObjectPath MeshPath;

	UPROPERTY()
	FSoftObjectPath MaterialPath;

	friend uint32 GetTypeHash(const FVoxelMeshComponentIdentifier& Identifier)
	{
		uint32 H = GetTypeHash(Identifier.MeshPath);
		H = HashCombine(H, GetTypeHash(Identifier.MaterialPath));
		H = HashCombine(H, GetTypeHash(Identifier.Transform));
		return H;
	}
};

USTRUCT()
struct VOXELSTREAMING_API FVoxelActorIdentifier
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVoxelMeshComponentIdentifier> Components;

	friend uint32 GetTypeHash(const FVoxelActorIdentifier& Identifier)
	{
		uint32 H = 0;
		for (const FVoxelMeshComponentIdentifier& C : Identifier.Components)
			H = HashCombine(H, GetTypeHash(C));
		return H;
	}
};

USTRUCT()
struct VOXELSTREAMING_API FVoxelModifierCacheEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FVoxelActorIdentifier Identifier;

	UPROPERTY()
	FVoxelModifierData BakedData;

	// Soft ptr serializes with level; resolved at runtime for BeginPlay hiding.
	UPROPERTY()
	TSoftObjectPtr<AActor> SourceActor;
};
