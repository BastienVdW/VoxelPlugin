// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "System/VoxelChunkActorSubsystem.h"

#include "Actor/VoxelChunkActor.h"
#include "Engine/World.h"
#include "Material/VoxelMaterialRegistry.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Settings/VoxelDeveloperSettings.h"
#include "UObject/SoftObjectPath.h"

void UVoxelChunkActorSubsystem::Deinitialize()
{
	Super::Deinitialize();
	
	ActiveChunks.Empty();
	Pool.Empty();
}

void UVoxelChunkActorSubsystem::SetMaterial(uint16 MaterialHash, UMaterialInterface* Material)
{
	MaterialRegistry.Add(MaterialHash, Material);
}

UMaterialInterface* UVoxelChunkActorSubsystem::ResolveMaterial(uint16 Hash)
{
    if (Hash == 0) return nullptr;

    // Already cached?
    if (TObjectPtr<UMaterialInterface>* Cached = MaterialRegistry.Find(Hash))
        return Cached->Get();

    // Resolve from registry path
    const FSoftObjectPath* Path = FVoxelMaterialRegistry::Find(Hash);
    if (!Path) return nullptr;

    UMaterialInterface* Mat = Cast<UMaterialInterface>(Path->TryLoad());
    if (Mat) MaterialRegistry.Add(Hash, Mat);
    return Mat;
}

AVoxelChunkActor* UVoxelChunkActorSubsystem::GetOrSpawnActor()
{
	if (Pool.Num() > 0)
	{
		return Pool.Pop(EAllowShrinking::No);
	}
	UWorld* World = GetWorld();
	check(World);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AVoxelChunkActor>(Params);
}

void UVoxelChunkActorSubsystem::ApplyMesh(const FVoxelRenderMeshResult& Result)
{
	check(IsInGameThread());

	AVoxelChunkActor* Actor = nullptr;
	if (TObjectPtr<AVoxelChunkActor>* Found = ActiveChunks.Find(Result.ChunkCoord))
	{
		Actor = Found->Get();
		Actor->ProceduralMesh->ClearAllMeshSections();
	}
	else
	{
		Actor = GetOrSpawnActor();
		ActiveChunks.Add(Result.ChunkCoord, Actor);
	}

	Actor->SetActorLocation(Result.ChunkOriginWorld);
	Actor->SetActorHiddenInGame(false);

	int32 SectionIndex = 0;
	for (const auto& [MatIndex, Section] : Result.Sections)
	{
		Actor->ProceduralMesh->CreateMeshSection(
			SectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UV0,
			/*VertexColors=*/TArray<FColor>{},
			/*Tangents=*/TArray<FProcMeshTangent>{},
			/*bCreateCollision=*/false
		);

		if (UMaterialInterface* Mat = ResolveMaterial(MatIndex))
		{
			Actor->ProceduralMesh->SetMaterial(SectionIndex, Mat);
		}
		++SectionIndex;
	}
}

void UVoxelChunkActorSubsystem::ReleaseChunkActor(FIntVector ChunkCoord)
{
	check(IsInGameThread());

	TObjectPtr<AVoxelChunkActor>* Found = ActiveChunks.Find(ChunkCoord);
	if (!Found) return;

	AVoxelChunkActor* Actor = Found->Get();
	Actor->SetActorHiddenInGame(true);
	Actor->ProceduralMesh->ClearAllMeshSections();
	ActiveChunks.Remove(ChunkCoord);

	const int32 PoolMax = GetDefault<UVoxelDeveloperSettings>()->ChunkActorPoolSize;
	if (Pool.Num() < PoolMax)
	{
		Pool.Add(Actor);
	}
	else
	{
		Actor->Destroy();
	}
}
