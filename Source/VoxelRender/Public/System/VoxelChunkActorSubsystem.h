// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Types/VoxelRenderTypes.h"
#include "VoxelChunkActorSubsystem.generated.h"

class AVoxelChunkActor;
class UMaterialInterface;

UCLASS()
class VOXELRENDER_API UVoxelChunkActorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	// Called from game thread after mesh generation completes.
	void ApplyMesh(const FVoxelRenderMeshResult& Result);

	// Called when a chunk is evicted — hides actor and returns it to pool.
	void ReleaseChunkActor(FIntVector ChunkCoord);

	// Register a material for a given FVoxelMaterialRegistry hash (called externally if needed).
	void SetMaterial(uint16 MaterialHash, UMaterialInterface* Material);

	virtual void Deinitialize() override;

private:
	AVoxelChunkActor*  GetOrSpawnActor();
	UMaterialInterface* ResolveMaterial(uint16 Hash); // loads from registry on first use

	UPROPERTY()
	TMap<FIntVector, TObjectPtr<AVoxelChunkActor>> ActiveChunks;

	UPROPERTY()
	TArray<TObjectPtr<AVoxelChunkActor>> Pool;
	TMap<uint16, TObjectPtr<UMaterialInterface>> MaterialRegistry;

};
