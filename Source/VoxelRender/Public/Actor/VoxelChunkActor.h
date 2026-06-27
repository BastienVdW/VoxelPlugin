// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelChunkActor.generated.h"

class UProceduralMeshComponent;

UCLASS()
class VOXELRENDER_API AVoxelChunkActor : public AActor
{
	GENERATED_BODY()
public:
	AVoxelChunkActor();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> ProceduralMesh;
};
