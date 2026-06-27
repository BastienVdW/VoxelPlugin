// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"

// Per-voxel material weight for blending.
struct VOXELCORE_API FMaterialWeight
{
    uint8 MaterialIndex = 0;
    uint8 Weight        = 0; // 0–255; highest weight wins at mesh time
};

// Per-voxel data. Fixed-size for memcpy-safe chunk snapshots.
struct VOXELCORE_API FVoxel
{
    float           Density      = 0.f;  // >0 solid, <=0 empty
    uint8           SurfaceType  = 0;    // legacy surface category (rock, snow, …)
    uint8           Flags        = 0;    // bit0=solid override, bit1=sensor, bits2-7 reserved
    uint16          MaterialHash = 0;    // FVoxelMaterialRegistry key; 0 = no material (fills 2-byte pad)
    FMaterialWeight Materials[2];        // per-voxel material weights
    FVector2f       UV           = FVector2f::ZeroVector; // baked from source mesh; zero if procedural

    bool IsSolid()  const { return Density > 0.f || (Flags & 0x01); }
    bool IsSensor() const { return (Flags & 0x02) != 0; }
};
// Layout: float(4) + uint8(1) + uint8(1) + pad(2) + FMaterialWeight[2](4) + FVector2f(8) = 20
// FVoxel is in-memory only — not serialized to disk or network. Safe to change layout.
static_assert(sizeof(FVoxel) == 20, "FVoxel layout changed — verify all memcpy snapshot sites");

// Dense N^3 voxel array owned by one chunk.
struct VOXELCORE_API FVoxelChunk
{
    FIntVector  ChunkCoord;          // grid-space integer coordinate
    int32       Resolution = 18;     // total voxels per axis = CanonicalResolution + 2*BorderSize
    int32       BorderSize = 1;      // extra voxel ring on each side for gradient/seam
    float       VoxelSize  = 25.f;   // centimeters per voxel
    bool        bDirty          = false;
    bool        bHasSolidVoxels = false; // set after each bake; false = all density <= 0

    TArray<FVoxel> Voxels; // size = Resolution^3, index = x + y*R + z*R*R

    void Init(FIntVector InCoord, int32 InCanonicalResolution, float InVoxelSize, int32 InBorderSize = 1);
    FVoxel& At(int32 X, int32 Y, int32 Z);
    const FVoxel& At(int32 X, int32 Y, int32 Z) const;
    FVector ChunkOriginWorld() const;  // bottom-corner world position
    bool IsValid() const { return Voxels.Num() == Resolution * Resolution * Resolution; }
};
