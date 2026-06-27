// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "SDF/VoxelSDFBaker.h"

namespace VoxelSDF
{

float PointTriangleSqDist(const FVector& P, const FVector& A, const FVector& B, const FVector& C,
                           FVector& OutClosest, float& OutU, float& OutV)
{
    FVector AB = B - A, AC = C - A, AP = P - A;
    float d1 = FVector::DotProduct(AB, AP), d2 = FVector::DotProduct(AC, AP);
    if (d1 <= 0 && d2 <= 0) { OutClosest = A; OutU = 0; OutV = 0; return FVector::DistSquared(P, A); }
    FVector BP = P - B;
    float d3 = FVector::DotProduct(AB, BP), d4 = FVector::DotProduct(AC, BP);
    if (d3 >= 0 && d4 <= d3) { OutClosest = B; OutU = 1; OutV = 0; return FVector::DistSquared(P, B); }
    FVector CP = P - C;
    float d5 = FVector::DotProduct(AB, CP), d6 = FVector::DotProduct(AC, CP);
    if (d6 >= 0 && d5 <= d6) { OutClosest = C; OutU = 0; OutV = 1; return FVector::DistSquared(P, C); }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
    {
        float v = d1 / (d1 - d3);
        OutClosest = A + AB * v; OutU = v; OutV = 0;
        return FVector::DistSquared(P, OutClosest);
    }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
    {
        float v = d2 / (d2 - d6);
        OutClosest = A + AC * v; OutU = 0; OutV = v;
        return FVector::DistSquared(P, OutClosest);
    }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
    {
        float v = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        OutClosest = B + (C - B) * v; OutU = 1.f - v; OutV = v;
        return FVector::DistSquared(P, OutClosest);
    }
    float denom = 1.f / (va + vb + vc);
    OutU = vb * denom;  // weight for B
    OutV = vc * denom;  // weight for C
    OutClosest = A + AB * OutU + AC * OutV;
    return FVector::DistSquared(P, OutClosest);
}

bool RayTriangle(const FVector& Orig, const FVector& Dir,
                 const FVector& A, const FVector& B, const FVector& C,
                 float& OutT)
{
    const float Eps = 1e-6f;
    FVector E1 = B - A, E2 = C - A, H = FVector::CrossProduct(Dir, E2);
    float Det = FVector::DotProduct(E1, H);
    if (FMath::Abs(Det) < Eps) return false;
    float InvDet = 1.f / Det;
    FVector S = Orig - A;
    float U = InvDet * FVector::DotProduct(S, H);
    if (U < 0 || U > 1) return false;
    FVector Q = FVector::CrossProduct(S, E1);
    float V = InvDet * FVector::DotProduct(Dir, Q);
    if (V < 0 || U + V > 1) return false;
    OutT = InvDet * FVector::DotProduct(E2, Q);
    return OutT > Eps;
}

void BakeTriangleMeshSDF(const TArray<FVector>& LocalVerts, const TArray<int32>& Indices,
                          const TArray<FVector2D>& SourceUVs,
                          FVoxelModifierData& OutModifier, int32 Resolution)
{
    if (LocalVerts.IsEmpty() || Indices.Num() < 3)
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelSDFBaker: no triangles to bake"));
        return;
    }

    const bool bHasUVs = (SourceUVs.Num() == LocalVerts.Num());

    FBox LocalBounds(EForceInit::ForceInit);
    for (const FVector& V : LocalVerts) LocalBounds += V;
    LocalBounds = LocalBounds.ExpandBy(1.f); // small padding so boundary voxels aren't clamped

    OutModifier.SDF.Resolution  = Resolution;
    OutModifier.SDF.LocalBounds = LocalBounds;
    const int32 TotalVoxels = Resolution * Resolution * Resolution;
    OutModifier.SDF.Samples.SetNumUninitialized(TotalVoxels);
    if (bHasUVs) OutModifier.SDF.UVSamples.SetNumUninitialized(TotalVoxels);

    FVector Extent = LocalBounds.GetExtent() * 2.f;
    FVector Min    = LocalBounds.Min;
    const int32 NumTris = Indices.Num() / 3;

    for (int32 Iz = 0; Iz < Resolution; ++Iz)
    for (int32 Iy = 0; Iy < Resolution; ++Iy)
    for (int32 Ix = 0; Ix < Resolution; ++Ix)
    {
        FVector P = Min + FVector(
            (Ix + 0.5f) / Resolution * Extent.X,
            (Iy + 0.5f) / Resolution * Extent.Y,
            (Iz + 0.5f) / Resolution * Extent.Z);

        float MinDist2  = FLT_MAX;
        int32 ClosestTi = 0;
        float ClosestU  = 0, ClosestV = 0;

        for (int32 Ti = 0; Ti < NumTris; ++Ti)
        {
            FVector Cl; float U, V;
            float D2 = PointTriangleSqDist(P,
                LocalVerts[Indices[Ti*3]],
                LocalVerts[Indices[Ti*3+1]],
                LocalVerts[Indices[Ti*3+2]], Cl, U, V);
            if (D2 < MinDist2) { MinDist2 = D2; ClosestTi = Ti; ClosestU = U; ClosestV = V; }
        }

        // Sign via ray cast: odd intersection count = inside.
        // Use a slightly off-axis direction to avoid hitting shared triangle edges
        // exactly (which would double-count, flipping the parity incorrectly).
        static const FVector RayDirs[] = {
            FVector(1.f,  1e-4f, 0.f),
            FVector(0.f,  1.f,   1e-4f),
            FVector(1e-4f, 0.f,  1.f),
        };

        int32 VoteInside = 0;
        for (const FVector& CastDir : RayDirs)
        {
            int32 Hits = 0;
            float T;
            for (int32 Ti = 0; Ti < NumTris; ++Ti)
            {
                if (RayTriangle(P, CastDir,
                    LocalVerts[Indices[Ti*3]],
                    LocalVerts[Indices[Ti*3+1]],
                    LocalVerts[Indices[Ti*3+2]], T))
                {
                    ++Hits;
                }
            }
            if (Hits % 2 == 1) ++VoteInside;
        }

        // Standard SDF: negative inside, positive outside
        const int32 VoxelIdx = Ix + Iy * Resolution + Iz * Resolution * Resolution;
        float Sign = (VoteInside >= 2) ? -1.f : 1.f;
        OutModifier.SDF.Samples[VoxelIdx] = Sign * FMath::Sqrt(MinDist2);

        if (bHasUVs)
        {
            const int32 I0 = Indices[ClosestTi * 3 + 0];
            const int32 I1 = Indices[ClosestTi * 3 + 1];
            const int32 I2 = Indices[ClosestTi * 3 + 2];
            const float W  = 1.f - ClosestU - ClosestV;
            OutModifier.SDF.UVSamples[VoxelIdx] = FVector2f(
                FVector2D(SourceUVs[I0]) * W +
                FVector2D(SourceUVs[I1]) * ClosestU +
                FVector2D(SourceUVs[I2]) * ClosestV);
        }
    }
}

} // namespace VoxelSDF
