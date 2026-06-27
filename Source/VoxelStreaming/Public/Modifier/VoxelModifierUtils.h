// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Modifier/VoxelModifierTypes.h"

class AActor;
class UObject;

namespace Voxel::Modifier::Utils
{
    // Builds and registers a modifier derived from Owner's StaticMeshComponent (for MeshSDF) or Params alone.
    // Returns an invalid handle if the streaming subsystem or grid is not available.
    VOXELSTREAMING_API FModifierHandle AddModifier(const AActor* Owner, const FVoxelModifierParameters& Params);

    // Registers a modifier from already-baked data (e.g. procedurally generated SDF).
    // Returns an invalid handle if the streaming subsystem or grid is not available.
    VOXELSTREAMING_API FModifierHandle AddModifierWithData(const UObject* WorldContext, FVoxelModifierData Data);

    // Bakes mesh SDF from Owner's StaticMeshComponent into OutData.
    // Returns false if no valid mesh component is found or baking produces no geometry.
    VOXELSTREAMING_API bool BuildModifierData(const AActor* Owner, const FVoxelModifierParameters& Params, FVoxelModifierData& OutData);

    VOXELSTREAMING_API void RemoveModifier(const UObject* WorldContext, FModifierHandle Handle);
} // namespace Voxel::Modifier::Utils
