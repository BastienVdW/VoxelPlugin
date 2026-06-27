// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Modifier/VoxelModifierCache.h"
#include "VoxelModifierCacheActor.generated.h"

// Level-persistent actor that stores pre-baked modifier data for static mesh actors.
// On BeginPlay: hides source actors, registers modifiers, then destroys itself.
// Populated and kept up-to-date by UVoxelEditorWorldSubsystem in the editor.
UCLASS(NotPlaceable)
class VOXELSTREAMING_API AVoxelModifierCacheActor : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="Voxel")
    TMap<FName, FVoxelModifierCacheEntry> Entries;

protected:
    virtual void BeginPlay() override;
};
