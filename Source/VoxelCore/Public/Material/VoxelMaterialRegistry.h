// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once
#include "CoreMinimal.h"

// Lightweight registry mapping a uint16 hash to a material soft path.
// Written on the game thread (BeginPlay), read from any thread (render).
// Hash 0 is reserved for "no material" (default gray).
class VOXELCORE_API FVoxelMaterialRegistry
{
public:
    // Register a soft path and return its stable uint16 hash.
    // If the path is already registered the existing hash is returned.
    static uint16 Register(const FSoftObjectPath& Path);

    // Returns nullptr if the hash is not registered or is 0.
    static const FSoftObjectPath* Find(uint16 Hash);

    static void Clear();

private:
    static FCriticalSection         Mutex;
    static TMap<uint16, FSoftObjectPath> HashToPath;

    static uint16 ComputeHash(const FSoftObjectPath& Path);
};
