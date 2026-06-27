// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Modifier/VoxelModifierTypes.h"

FBoxSphereBounds FVoxelModifierData::GetWorldBounds() const
{
    switch (Params.Type)
    {
    case EModifierType::PrimitiveSphere:
    {
        float Radius = Transform.GetScale3D().X;
        return FBoxSphereBounds(Transform.GetLocation(), FVector(Radius), Radius);
    }
    case EModifierType::PrimitiveBox:
    {
        FVector HalfExtents = Transform.GetScale3D();
        FBox Box(Transform.GetLocation() - HalfExtents, Transform.GetLocation() + HalfExtents);
        return FBoxSphereBounds(Box);
    }
    case EModifierType::MeshSDF:
    {
        if (SDF.LocalBounds.IsValid)
        {
            FBox World = SDF.LocalBounds.TransformBy(Transform.ToMatrixWithScale());
            return FBoxSphereBounds(World);
        }
        // SDF not baked yet — return degenerate bounds so no chunks are incorrectly
        // marked dirty. The BeginPlay log will show why baking was skipped.
        return FBoxSphereBounds(EForceInit::ForceInit);
    }
    default:
        return FBoxSphereBounds(Transform.GetLocation(), FVector(100.f), 100.f);
    }
}
