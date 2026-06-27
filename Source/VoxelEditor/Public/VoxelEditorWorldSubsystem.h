// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Modifier/VoxelModifierTypes.h"
#include "Modifier/VoxelModifierCache.h"
#include "VoxelEditorWorldSubsystem.generated.h"

class AStaticMeshActor;
class AVoxelModifierCacheActor;

struct FVoxelPendingBake
{
	AStaticMeshActor*     Actor     = nullptr;
	FName                 Label;
	FVoxelActorIdentifier Identifier;
};

struct FVoxelBakeResult
{
	bool               bSuccess = false;
	FVoxelModifierData BakedData;
};

UCLASS()
class VOXELEDITOR_API UVoxelEditorWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UWorldSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UVoxelEditorWorldSubsystem, STATGROUP_Tickables); }

private:
	TWeakObjectPtr<AVoxelModifierCacheActor> CacheActor;
	TSet<FName> SeenLabels;
	TArray<FVoxelPendingBake> PendingBakes;
	TArray<FVoxelBakeResult> BakeResults;

	AVoxelModifierCacheActor* GetOrCreateCacheActor();
	static FVoxelActorIdentifier BuildActorIdentifier(const AStaticMeshActor* Actor);
	static void CollectPendingBakes(UWorld* World, AVoxelModifierCacheActor* Cache, TSet<FName>& OutSeenLabels, TArray<FVoxelPendingBake>& OutPendings);
	static void BakePending(const TArray<FVoxelPendingBake>& Pending, TArray<FVoxelBakeResult>& OutResults);
};
