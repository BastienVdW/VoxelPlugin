// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Modifier/VoxelModifierUtils.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Grid/VoxelGrid.h"
#include "SDF/VoxelSDFBaker.h"
#include "Settings/VoxelDeveloperSettings.h"
#include "StaticMeshResources.h"
#include "Streaming/VoxelStreamingSubsystem.h"

namespace
{
void BakeMeshSDF(UStaticMeshComponent* MeshComp, FVoxelModifierData& OutModifier, int32 Resolution)
{
    UStaticMesh* Mesh = MeshComp->GetStaticMesh();
    if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelModifierUtils: BakeMeshSDF - no valid mesh LOD data"));
        return;
    }

    const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];

    TArray<FVector> Verts;
    TArray<int32>   Indices;
    {
        const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
        Verts.SetNum(PosBuffer.GetNumVertices());
        for (uint32 i = 0; i < PosBuffer.GetNumVertices(); ++i)
            Verts[i] = FVector(PosBuffer.VertexPosition(i));

        TArray<uint32> RawIdx;
        LOD.IndexBuffer.GetCopy(RawIdx);
        Indices.SetNum(RawIdx.Num());
        for (int32 i = 0; i < RawIdx.Num(); ++i)
            Indices[i] = (int32)RawIdx[i];
    }

    if (Verts.IsEmpty() || Indices.Num() < 3)
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelModifierUtils: BakeMeshSDF - mesh has no triangles"));
        return;
    }

    TArray<FVector2D> SourceUVs;
    if (LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() == (uint32)Verts.Num())
    {
        const FStaticMeshVertexBuffer& UVBuf = LOD.VertexBuffers.StaticMeshVertexBuffer;
        SourceUVs.SetNum(Verts.Num());
        for (int32 i = 0; i < Verts.Num(); ++i)
            SourceUVs[i] = FVector2D(UVBuf.GetVertexUV(i, 0));
    }

    VoxelSDF::BakeTriangleMeshSDF(Verts, Indices, SourceUVs, OutModifier, Resolution);
    UE_LOG(LogTemp, Log, TEXT("VoxelModifierUtils: BakeMeshSDF complete (%d³, %d triangles)"),
        Resolution, Indices.Num() / 3);
}
} // namespace

namespace Voxel::Modifier::Utils
{

FModifierHandle AddModifierWithData(const UObject* WorldContext, FVoxelModifierData Data)
{
    if (!IsValid(WorldContext))
    {
        UE_LOG(LogTemp, Error, TEXT("VoxelModifierUtils: AddModifierWithData — invalid WorldContext"));
        return FModifierHandle::Invalid();
    }

    UVoxelStreamingSubsystem* StreamingSystem = UWorld::GetSubsystem<UVoxelStreamingSubsystem>(WorldContext->GetWorld());
    if (!StreamingSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("VoxelModifierUtils [%s]: UVoxelStreamingSubsystem not found"),
            *WorldContext->GetName());
        return FModifierHandle::Invalid();
    }

    FModifierHandle Handle = StreamingSystem->GetMutableGrid().AddModifier(Data);
    UE_LOG(LogTemp, Log, TEXT("VoxelModifierUtils [%s]: AddModifierWithData handle=%u"),
        *WorldContext->GetName(), Handle.Id);
    return Handle;
}

bool BuildModifierData(const AActor* Owner, const FVoxelModifierParameters& Params, FVoxelModifierData& OutData)
{
    if (!IsValid(Owner))
    {
        UE_LOG(LogTemp, Error, TEXT("VoxelModifierUtils: BuildModifierData — invalid owner"));
        return false;
    }

    OutData = FVoxelModifierData{};
    OutData.Params = Params;

    if (Params.Type == EModifierType::MeshSDF)
    {
        UStaticMeshComponent* MeshComp = Owner->FindComponentByClass<UStaticMeshComponent>();
        if (!MeshComp || !MeshComp->GetStaticMesh())
        {
            UE_LOG(LogTemp, Warning, TEXT("VoxelModifierUtils [%s]: BuildModifierData — no valid StaticMeshComponent"),
                *Owner->GetName());
            return false;
        }

        OutData.Transform = MeshComp->GetComponentTransform();
        const int32 BakeRes = GetDefault<UVoxelDeveloperSettings>()->SDFBakeResolution;
        BakeMeshSDF(MeshComp, OutData, BakeRes);

        if (OutData.SDF.Samples.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("VoxelModifierUtils [%s]: BuildModifierData — bake produced no samples"),
                *Owner->GetName());
            return false;
        }

        if (UMaterialInterface* Mat = MeshComp->GetMaterial(0))
            OutData.MaterialPath = FSoftObjectPath(Mat);

        UE_LOG(LogTemp, Log, TEXT("VoxelModifierUtils [%s]: BuildModifierData complete — Resolution=%d Samples=%d"),
            *Owner->GetName(), OutData.SDF.Resolution, OutData.SDF.Samples.Num());
    }

    return true;
}

FModifierHandle AddModifier(const AActor* Owner, const FVoxelModifierParameters& Params)
{
    if (!IsValid(Owner))
    {
        UE_LOG(LogTemp, Error, TEXT("VoxelModifierUtils: AddModifier — invalid owner"));
        return FModifierHandle::Invalid();
    }

    FVoxelModifierData Data;
    if (!BuildModifierData(Owner, Params, Data))
        return FModifierHandle::Invalid();

    if (Params.Type == EModifierType::MeshSDF)
    {
        // Hide the source mesh — it is now represented by the voxel modifier.
        if (UStaticMeshComponent* MeshComp = Owner->FindComponentByClass<UStaticMeshComponent>())
            MeshComp->SetHiddenInGame(true);
    }

    return AddModifierWithData(Owner, MoveTemp(Data));
}

void RemoveModifier(const UObject* WorldContext, FModifierHandle Handle)
{
    if (!Handle.IsValid())
        return;

    if (UVoxelStreamingSubsystem* StreamingSystem = UWorld::GetSubsystem<UVoxelStreamingSubsystem>(WorldContext->GetWorld()))
    {
        StreamingSystem->GetMutableGrid().RemoveModifier(Handle);
    }
}

} // namespace Voxel::Modifier::Utils
