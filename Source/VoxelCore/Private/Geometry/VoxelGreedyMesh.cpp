// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Geometry/VoxelGreedyMesh.h"

namespace
{
    // Helper: set a component of FIntVector by axis index (0=X, 1=Y, 2=Z)
    FORCEINLINE void SetIVComp(FIntVector& V, int32 Axis, int32 Val)
    {
        int32* Arr = &V.X;
        Arr[Axis] = Val;
    }

    // Helper: get a component of FIntVector by axis index
    FORCEINLINE int32 GetIVComp(const FIntVector& V, int32 Axis)
    {
        const int32* Arr = &V.X;
        return Arr[Axis];
    }
}

FVoxelMeshData FVoxelGreedyMesh::ExtractSurface(const FVoxelChunk& Chunk)
{
    FVoxelMeshData Mesh;
    const int32 R  = Chunk.Resolution;
    const int32 B  = Chunk.BorderSize;
    const float VS = Chunk.VoxelSize;

    // Visit all 6 face orientations: Axis in {0,1,2}, bFront in {0,1}
    // bFront=0: face between solid voxel at D and empty voxel at D+1 -> normal in +Axis
    // bFront=1: face between empty at D and solid at D+1 -> normal in -Axis
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const int32 AxisU = (Axis + 1) % 3;  // first perpendicular axis
        const int32 AxisV = (Axis + 2) % 3;  // second perpendicular axis

        for (int32 bFront = 0; bFront < 2; ++bFront)
        {
            // Scan each canonical slice along Axis.
            // D goes from B-1 to R-B-1: face between voxel D and D+1.
            for (int32 D = B - 1; D < R - B; ++D)
            {
                // Build face mask for this slice.
                // mask[U + V*R]: valid face at (D, U, V); Hash 0 = no face.
                struct FFace { uint16 Hash; bool bValid; };
                TArray<FFace> Mask;
                Mask.SetNum(R * R);

                for (int32 Iv = B; Iv < R - B; ++Iv)
                for (int32 Iu = B; Iu < R - B; ++Iu)
                {
                    // Build voxel coords for "this" (PA) and "neighbour" (PB)
                    FIntVector PA(0, 0, 0), PB(0, 0, 0);
                    SetIVComp(PA, Axis,  D);
                    SetIVComp(PA, AxisU, Iu);
                    SetIVComp(PA, AxisV, Iv);
                    SetIVComp(PB, Axis,  D + 1);
                    SetIVComp(PB, AxisU, Iu);
                    SetIVComp(PB, AxisV, Iv);

                    auto IsSolid = [&](const FIntVector& P) -> bool {
                        if (GetIVComp(P, Axis) < 0 || GetIVComp(P, Axis) >= R) return false;
                        return Chunk.At(P.X, P.Y, P.Z).Density > 0.f;
                    };
                    auto GetHash = [&](const FIntVector& P) -> uint16 {
                        return Chunk.At(P.X, P.Y, P.Z).MaterialHash;
                    };

                    FFace& F = Mask[Iu + Iv * R];
                    if (bFront == 0)
                    {
                        // +Axis face: solid at D, empty at D+1
                        F.bValid = IsSolid(PA) && !IsSolid(PB);
                        F.Hash   = F.bValid ? GetHash(PA) : 0;
                    }
                    else
                    {
                        // -Axis face: empty at D, solid at D+1
                        F.bValid = !IsSolid(PA) && IsSolid(PB);
                        F.Hash   = F.bValid ? GetHash(PB) : 0;
                    }
                }

                // Greedy merge and quad emission
                TArray<bool> Used;
                Used.SetNumZeroed(R * R);

                for (int32 Iv = B; Iv < R - B; ++Iv)
                for (int32 Iu = B; Iu < R - B; ++Iu)
                {
                    const int32 Idx = Iu + Iv * R;
                    if (!Mask[Idx].bValid || Used[Idx]) continue;

                    const uint16 RefHash = Mask[Idx].Hash;

                    // Expand width (U direction)
                    int32 Width = 1;
                    while (Iu + Width < R - B)
                    {
                        const int32 NIdx = (Iu + Width) + Iv * R;
                        if (!Mask[NIdx].bValid || Used[NIdx] || Mask[NIdx].Hash != RefHash) break;
                        ++Width;
                    }

                    // Expand height (V direction)
                    int32 Height = 1;
                    while (Iv + Height < R - B)
                    {
                        bool bRowOk = true;
                        for (int32 K = 0; K < Width; ++K)
                        {
                            const int32 RIdx = (Iu + K) + (Iv + Height) * R;
                            if (!Mask[RIdx].bValid || Used[RIdx] || Mask[RIdx].Hash != RefHash)
                            { bRowOk = false; break; }
                        }
                        if (!bRowOk) break;
                        ++Height;
                    }

                    // Mark used
                    for (int32 DV = 0; DV < Height; ++DV)
                    for (int32 DU = 0; DU < Width; ++DU)
                        Used[(Iu + DU) + (Iv + DV) * R] = true;

                    // Emit quad: 4 corners at the face boundary.
                    // Face sits between slice D and D+1 along Axis.
                    // bFront==0: face is on the +Axis side of slice D -> pos = (D+1)*VS
                    // bFront==1: face is on the -Axis side of slice D+1 -> pos = (D+1)*VS (same)
                    const float FacePos = (D + 1) * VS;

                    auto Corner = [&](int32 CU, int32 CV) -> FVector3f
                    {
                        FVector P(0.0, 0.0, 0.0);
                        P[Axis]  = FacePos;
                        P[AxisU] = (Iu + CU) * VS;
                        P[AxisV] = (Iv + CV) * VS;
                        return FVector3f(P);
                    };

                    // Corners: BL, BR, TR, TL
                    const FVector3f C0 = Corner(0,     0);
                    const FVector3f C1 = Corner(Width, 0);
                    const FVector3f C2 = Corner(Width, Height);
                    const FVector3f C3 = Corner(0,     Height);

                    // Fetch UVs from the solid voxel on the appropriate slice
                    auto GetUV = [&](int32 CU, int32 CV) -> FVector2f
                    {
                        FIntVector P(0, 0, 0);
                        SetIVComp(P, Axis,  FMath::Clamp(D + (bFront == 1 ? 1 : 0), 0, R - 1));
                        SetIVComp(P, AxisU, FMath::Clamp(Iu + CU, 0, R - 1));
                        SetIVComp(P, AxisV, FMath::Clamp(Iv + CV, 0, R - 1));
                        return Chunk.At(P.X, P.Y, P.Z).UV;
                    };

                    const int32 Base = Mesh.Vertices.Num();
                    Mesh.Vertices.Add(C0); Mesh.UVs.Add(GetUV(0,      0));
                    Mesh.Vertices.Add(C1); Mesh.UVs.Add(GetUV(Width,  0));
                    Mesh.Vertices.Add(C2); Mesh.UVs.Add(GetUV(Width,  Height));
                    Mesh.Vertices.Add(C3); Mesh.UVs.Add(GetUV(0,      Height));

                    // Two triangles — winding chosen so the render subsystem's
                    // CrossProduct(C-A, B-A) normal points in the expected direction.
                    // bFront==0 (+Axis face): normal = +Axis -> winding Base+0,+3,+2 / Base+0,+2,+1
                    // bFront==1 (-Axis face): flip -> Base+0,+1,+2 / Base+0,+2,+3
                    // (Verify in-engine and flip both pairs if faces appear black/inverted.)
                    if (bFront == 0)
                    {
                        Mesh.Triangles.Add(Base+0); Mesh.Triangles.Add(Base+3); Mesh.Triangles.Add(Base+2);
                        Mesh.Triangles.Add(Base+0); Mesh.Triangles.Add(Base+2); Mesh.Triangles.Add(Base+1);
                    }
                    else
                    {
                        Mesh.Triangles.Add(Base+0); Mesh.Triangles.Add(Base+1); Mesh.Triangles.Add(Base+2);
                        Mesh.Triangles.Add(Base+0); Mesh.Triangles.Add(Base+2); Mesh.Triangles.Add(Base+3);
                    }

                    // Two triangles per quad -> two material hash entries
                    Mesh.TriangleMaterialHashes.Add(RefHash);
                    Mesh.TriangleMaterialHashes.Add(RefHash);
                }
            }
        }
    }

    return Mesh;
}
