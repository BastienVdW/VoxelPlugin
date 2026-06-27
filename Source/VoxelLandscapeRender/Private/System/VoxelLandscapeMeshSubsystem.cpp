// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "System/VoxelLandscapeMeshSubsystem.h"

#include "Actor/VoxelProceduralMeshActor.h"
#include "Engine/World.h"
#include "Misc/Crc.h"
#include "ProceduralMeshComponent.h"
#include "Settings/VoxelDeveloperSettings.h"

static FString MakeEntryKey(const FProceduralMeshActorKey& Key)
{
    return FString::Printf(TEXT("%d_%d_%d_%s"), Key.SuperChunkKey.X, Key.SuperChunkKey.Y, Key.SuperChunkKey.Z, *Key.LayerName.ToString());
}

void UVoxelLandscapeMeshSubsystem::Deinitialize()
{
    Super::Deinitialize();
	
    ProceduralMeshActorEntries.Reset();
}

/*static*/ FVector2D UVoxelLandscapeMeshSubsystem::ComputeActorOrigin(FIntVector MeshKey, float VoxelSize)
{
    const UVoxelDeveloperSettings* Settings = GetDefault<UVoxelDeveloperSettings>();
    const int32 ChunksPerActor = Settings->ChunksPerProceduralMeshActor;
    const float ChunkSize = Settings->ChunkResolution * VoxelSize;

    return FVector2D(
        FloorDiv(MeshKey.X, ChunksPerActor) * ChunksPerActor * ChunkSize,
        FloorDiv(MeshKey.Y, ChunksPerActor) * ChunksPerActor * ChunkSize);
}

void UVoxelLandscapeMeshSubsystem::UpdateChunkSection(FIntVector MeshKey, FName LayerName,
                                                  UMaterialInterface* Material,
                                                  FLandscapeLayerSection Section,
                                                  float VoxelSize)
{
    check(IsInGameThread());

    const UVoxelDeveloperSettings* Settings = GetDefault<UVoxelDeveloperSettings>();
    const int32 ChunksPerActor = Settings->ChunksPerProceduralMeshActor;
    const float ChunkSize = Settings->ChunkResolution * VoxelSize;

    const FIntVector SuperChunkKey(
        FloorDiv(MeshKey.X, ChunksPerActor),
        FloorDiv(MeshKey.Y, ChunksPerActor),
        FloorDiv(MeshKey.Z, ChunksPerActor));

    const FIntVector LocalPosition(
        PositiveMod(MeshKey.X, ChunksPerActor),
        PositiveMod(MeshKey.Y, ChunksPerActor),
        PositiveMod(MeshKey.Z, ChunksPerActor));

    const int32 SectionIndex = LocalPosition.X
                             + LocalPosition.Y * ChunksPerActor
                             + LocalPosition.Z * ChunksPerActor * ChunksPerActor;

    // Actor origin at the XY corner of the super-chunk group; Z stays at 0 since surface Z varies per cell.
    const FVector ActorOrigin(
        SuperChunkKey.X * ChunksPerActor * ChunkSize,
        SuperChunkKey.Y * ChunksPerActor * ChunkSize,
        0.f);

    const FProceduralMeshActorKey ActorKey{SuperChunkKey, LayerName};
    FProceduralMeshActorEntry& Entry = GetOrCreateEntry(ActorKey, ActorOrigin);
    if (!IsValid(Entry.Actor)) return;

    UProceduralMeshComponent* MeshComponent = Entry.Actor->MeshComponent;
    if (!IsValid(MeshComponent)) return;

    const int32 NewVertexCount = Section.Vertices.Num();
    const uint32 NewIndexHash = Section.Indices.Num() > 0
        ? FCrc::MemCrc32(Section.Indices.GetData(), Section.Indices.Num() * sizeof(int32))
        : 0u;
    const int32* PreviousVertexCount = Entry.SectionVertexCounts.Find(SectionIndex);
    const uint32* PreviousIndexHash = Entry.SectionIndexHashes.Find(SectionIndex);

    if (PreviousVertexCount && PreviousIndexHash &&
        *PreviousVertexCount == NewVertexCount && *PreviousIndexHash == NewIndexHash)
    {
        MeshComponent->UpdateMeshSection(SectionIndex, Section.Vertices, Section.Normals,
                                         Section.UVs, TArray<FColor>(), TArray<FProcMeshTangent>());
    }
    else
    {
        MeshComponent->CreateMeshSection(SectionIndex, Section.Vertices, Section.Indices,
                                         Section.Normals, Section.UVs,
                                         TArray<FColor>(), TArray<FProcMeshTangent>(),
                                         /*bCreateCollision=*/false);
        MeshComponent->SetMaterial(SectionIndex, Material);
        Entry.SectionVertexCounts.Add(SectionIndex, NewVertexCount);
        Entry.SectionIndexHashes.Add(SectionIndex, NewIndexHash);
    }
}

FProceduralMeshActorEntry& UVoxelLandscapeMeshSubsystem::GetOrCreateEntry(
    const FProceduralMeshActorKey& Key, FVector ActorOrigin)
{
    const FString EntryKey = MakeEntryKey(Key);
    FProceduralMeshActorEntry& Entry = ProceduralMeshActorEntries.FindOrAdd(EntryKey);

    if (!IsValid(Entry.Actor))
    {
        Entry.SectionVertexCounts.Reset();
        Entry.SectionIndexHashes.Reset();
        UWorld* World = GetWorld();
        if (IsValid(World))
        {
            Entry.Actor = World->SpawnActor<AVoxelProceduralMeshActor>(
                AVoxelProceduralMeshActor::StaticClass(),
                FTransform(ActorOrigin));
        }
    }

    return Entry;
}

/*static*/ int32 UVoxelLandscapeMeshSubsystem::FloorDiv(int32 Dividend, int32 Divisor)
{
    const int32 Quotient = Dividend / Divisor;
    // Adjust for negative values: C++ truncates toward zero, floor rounds toward -inf.
    return Quotient - (Dividend % Divisor != 0 && ((Dividend ^ Divisor) < 0) ? 1 : 0);
}

/*static*/ int32 UVoxelLandscapeMeshSubsystem::PositiveMod(int32 Value, int32 Modulus)
{
    return ((Value % Modulus) + Modulus) % Modulus;
}
