// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Material/VoxelMaterialRegistry.h"
#include "Misc/Crc.h"

FCriticalSection          FVoxelMaterialRegistry::Mutex;
TMap<uint16, FSoftObjectPath> FVoxelMaterialRegistry::HashToPath;

uint16 FVoxelMaterialRegistry::ComputeHash(const FSoftObjectPath& Path)
{
    const FString Str = Path.ToString();
    const uint32  H32 = FCrc::StrCrc32(*Str);
    // Fold to 16 bits; skip 0 (reserved for "no material")
    uint16 H = (uint16)((H32 ^ (H32 >> 16)) & 0xFFFF);
    return H != 0 ? H : 1;
}

uint16 FVoxelMaterialRegistry::Register(const FSoftObjectPath& Path)
{
    if (!Path.IsValid()) return 0;

    const uint16 Hash = ComputeHash(Path);
    FScopeLock Lock(&Mutex);
    HashToPath.FindOrAdd(Hash) = Path;
    return Hash;
}

const FSoftObjectPath* FVoxelMaterialRegistry::Find(uint16 Hash)
{
    if (Hash == 0) return nullptr;
    FScopeLock Lock(&Mutex);
    return HashToPath.Find(Hash);
}

void FVoxelMaterialRegistry::Clear()
{
    FScopeLock Lock(&Mutex);
    HashToPath.Empty();
}
