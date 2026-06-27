// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Component/VoxelObjectComponent.h"

#include "Modifier/VoxelModifierUtils.h"

UVoxelObjectComponent::UVoxelObjectComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UVoxelObjectComponent::BeginPlay()
{
    Super::BeginPlay();
    ModifierHandle = Voxel::Modifier::Utils::AddModifier(GetOwner(), Params);
}

void UVoxelObjectComponent::EndPlay(EEndPlayReason::Type EndPlayReason)
{
    Voxel::Modifier::Utils::RemoveModifier(this, ModifierHandle);
    Super::EndPlay(EndPlayReason);
}
