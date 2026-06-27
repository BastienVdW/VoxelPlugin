// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "VoxelEditorWorldSubsystem.h"

#include "Async/ParallelFor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Modifier/VoxelModifierCache.h"
#include "Modifier/VoxelModifierCacheActor.h"
#include "Modifier/VoxelModifierTypes.h"
#include "Modifier/VoxelModifierUtils.h"

bool UVoxelEditorWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
		return false;

	const UWorld* World = Cast<UWorld>(Outer);
	return GIsEditor && World && !World->IsGameWorld();
}

void UVoxelEditorWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UVoxelEditorWorldSubsystem::Tick(float DeltaTime)
{
	AVoxelModifierCacheActor* Cache = GetOrCreateCacheActor();
	if (!IsValid(Cache))
	{
		return;
	}

	CollectPendingBakes(GetWorld(), Cache, SeenLabels, PendingBakes);
	BakePending(PendingBakes, BakeResults);

	for (int32 i = 0; i < PendingBakes.Num(); ++i)
	{
		if (!BakeResults[i].bSuccess)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelEditorWorldSubsystem: bake failed for '%s'"), *PendingBakes[i].Label.ToString());
			continue;
		}

		FVoxelModifierCacheEntry& Entry = Cache->Entries.FindOrAdd(PendingBakes[i].Label);
		Entry.Identifier  = PendingBakes[i].Identifier;
		Entry.BakedData   = BakeResults[i].BakedData;
		Entry.SourceActor = TSoftObjectPtr<AActor>(PendingBakes[i].Actor);

		UE_LOG(LogTemp, Log, TEXT("VoxelEditorWorldSubsystem: cached '%s'"), *PendingBakes[i].Label.ToString());
	}

	for (auto It = Cache->Entries.CreateIterator(); It; ++It)
	{
		if (!SeenLabels.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

void UVoxelEditorWorldSubsystem::CollectPendingBakes(UWorld* World, AVoxelModifierCacheActor* Cache, TSet<FName>& OutSeenLabels, TArray<FVoxelPendingBake>& OutPendings)
{
	OutSeenLabels.Reset();
	OutPendings.Reset();
	
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* SMActor = *It;
		if (!IsValid(SMActor) || !SMActor->GetActorEnableCollision() || !SMActor->IsRootComponentStatic())
		{
			continue;
		}

		const UStaticMeshComponent* SMC = SMActor->GetStaticMeshComponent();
		if (!IsValid(SMC) || !SMC->IsPhysicsCollisionEnabled() ||
			SMC->GetCollisionObjectType() != ECC_WorldStatic ||
			!IsValid(SMC->GetStaticMesh()))
		{
			continue;
		}

		const FName Label = FName(*SMActor->GetActorLabel());
		OutSeenLabels.Add(Label);

		const FVoxelActorIdentifier NewIdentifier = BuildActorIdentifier(SMActor);

		if (const FVoxelModifierCacheEntry* Existing = Cache->Entries.Find(Label))
		{
			if (GetTypeHash(Existing->Identifier) == GetTypeHash(NewIdentifier))
			{
				continue;
			}
		}

		OutPendings.Add({ SMActor, Label, NewIdentifier });
	}
}

void UVoxelEditorWorldSubsystem::BakePending(const TArray<FVoxelPendingBake>& Pending, TArray<FVoxelBakeResult>& OutBakes)
{
	OutBakes.SetNum(Pending.Num(), EAllowShrinking::No);

	ParallelFor(Pending.Num(), [&Pending, &OutBakes](int32 Index)
	{
		FVoxelModifierParameters Params;
		Params.Type      = EModifierType::MeshSDF;
		Params.Operation = EModifierOp::Add;

		OutBakes[Index].bSuccess = Voxel::Modifier::Utils::BuildModifierData(
			Pending[Index].Actor, Params, OutBakes[Index].BakedData);
	});
}

AVoxelModifierCacheActor* UVoxelEditorWorldSubsystem::GetOrCreateCacheActor()
{
	if (CacheActor.IsValid())
	{
		return CacheActor.Get();
	}
	
	for (TActorIterator<AVoxelModifierCacheActor> It(GetWorld()); It; ++It)
	{
		CacheActor = *It;
		return CacheActor.Get();
	}

	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AVoxelModifierCacheActor* Actor = World->SpawnActor<AVoxelModifierCacheActor>(Params);
		CacheActor = Actor;
		return CacheActor.Get();
	}
	
	return nullptr;
}

FVoxelActorIdentifier UVoxelEditorWorldSubsystem::BuildActorIdentifier(const AStaticMeshActor* Actor)
{
	FVoxelActorIdentifier Identifier;

	TArray<UStaticMeshComponent*> Components;
	Actor->GetComponents<UStaticMeshComponent>(Components);

	for (const UStaticMeshComponent* SMC : Components)
	{
		if (!IsValid(SMC))
			continue;

		FVoxelMeshComponentIdentifier CompId;
		CompId.Transform  = SMC->GetComponentTransform();
		CompId.MeshPath   = SMC->GetStaticMesh() ? FSoftObjectPath(SMC->GetStaticMesh()) : FSoftObjectPath();
		CompId.MaterialPath = SMC->GetMaterial(0) ? FSoftObjectPath(SMC->GetMaterial(0)) : FSoftObjectPath();

		Identifier.Components.Add(MoveTemp(CompId));
	}

	return Identifier;
}
