// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Generation/LandscapeMeshGenerator.h"
#include "VoxelLandscapeMeshSubsystem.generated.h"

class AVoxelProceduralMeshActor;
class UMaterialInterface;

struct FProceduralMeshActorKey
{
    FIntVector SuperChunkKey;
    FName      LayerName;

    bool operator==(const FProceduralMeshActorKey& Other) const
    {
        return SuperChunkKey == Other.SuperChunkKey && LayerName == Other.LayerName;
    }
    friend uint32 GetTypeHash(const FProceduralMeshActorKey& Key)
    {
        return HashCombine(GetTypeHash(Key.SuperChunkKey), GetTypeHash(Key.LayerName));
    }
};


USTRUCT()
struct FProceduralMeshActorEntry
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<AVoxelProceduralMeshActor> Actor = nullptr;

    // Last uploaded topology per section index. ProceduralMeshComponent::UpdateMeshSection
    // cannot replace triangle indices, so topology changes must recreate the section.
    UPROPERTY()
    TMap<int32, int32> SectionVertexCounts;

    UPROPERTY()
    TMap<int32, uint32> SectionIndexHashes;
};

// Manages ProceduralMeshActors grouped into super-chunks (N×N×N chunks per actor per layer).
// Each chunk occupies a dedicated section, avoiding per-chunk actor overhead.
UCLASS()
class VOXELLANDSCAPERENDER_API UVoxelLandscapeMeshSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    // Computes the XY world origin of the ProceduralMeshActor that owns MeshKey.
    // Safe to call from any thread. Vertices passed to UpdateChunkSection must already
    // be relative to this origin (i.e. computed with this offset on the background thread).
    static FVector2D ComputeActorOrigin(FIntVector MeshKey, float VoxelSize);

    // Must be called on the game thread. Vertices must already be in actor-local space.
    void UpdateChunkSection(FIntVector MeshKey, FName LayerName, UMaterialInterface* Material,
                            FLandscapeLayerSection Section, float VoxelSize);

private:
    FProceduralMeshActorEntry& GetOrCreateEntry(const FProceduralMeshActorKey& Key, FVector ActorOrigin);

    static int32 FloorDiv(int32 Dividend, int32 Divisor);
    static int32 PositiveMod(int32 Value, int32 Modulus);

    // TMap value is a USTRUCT with UPROPERTY actor pointer — no separate GC anchor needed.
    UPROPERTY()
    TMap<FString, FProceduralMeshActorEntry> ProceduralMeshActorEntries;
};
