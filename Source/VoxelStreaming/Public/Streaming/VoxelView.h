// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "VoxelView.generated.h"

USTRUCT()
struct VOXELSTREAMING_API FVoxelView
{
    GENERATED_BODY()

    UPROPERTY() FVector Position    = FVector::ZeroVector;
    UPROPERTY() FVector Direction   = FVector::ForwardVector;
    UPROPERTY() float   NearRadius  = 2000.f;  // cm — high-res zone
    UPROPERTY() float   FarRadius   = 6000.f;  // cm — low-res zone (beyond = evict)
    UPROPERTY() float   NearVoxelSize = 25.f;  // cm
    UPROPERTY() float   FarVoxelSize  = 100.f; // cm
};
