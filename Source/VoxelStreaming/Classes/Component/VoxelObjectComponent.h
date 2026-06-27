// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Components/ActorComponent.h"
#include "Modifier/VoxelModifierTypes.h"

#include "VoxelObjectComponent.generated.h"

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class VOXELSTREAMING_API UVoxelObjectComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVoxelObjectComponent();

    UPROPERTY(EditAnywhere, Category="Voxel")
    FVoxelModifierParameters Params;

    // True = part of initial level state (kept on Reset). False = dynamic (removed on Reset).
    UPROPERTY(EditAnywhere, Category="Voxel")
    bool bStatic = true;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:
    FModifierHandle ModifierHandle;
};
