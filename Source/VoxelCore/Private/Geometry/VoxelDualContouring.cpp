// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Geometry/VoxelDualContouring.h"
#include "Math/UnrealMathUtility.h"

// ---------------------------------------------------------------------------
// Corner offsets (standard marching cubes order)
// ---------------------------------------------------------------------------
static const int32 GCornerDX[8] = { 0, 1, 1, 0,  0, 1, 1, 0 };
static const int32 GCornerDY[8] = { 0, 0, 1, 1,  0, 0, 1, 1 };
static const int32 GCornerDZ[8] = { 0, 0, 0, 0,  1, 1, 1, 1 };

// 12 edges: pairs of corner indices
static const int32 GEdgeC0[12] = { 0, 1, 2, 3,  4, 5, 6, 7,  0, 1, 2, 3 };
static const int32 GEdgeC1[12] = { 1, 2, 3, 0,  5, 6, 7, 4,  4, 5, 6, 7 };

// ---------------------------------------------------------------------------
// GradientAt — central differences, clamped to valid range
// ---------------------------------------------------------------------------
FVector3f FVoxelDualContouring::GradientAt(const FVoxelChunk& Chunk, int32 X, int32 Y, int32 Z)
{
    auto D = [&](int32 x, int32 y, int32 z) -> float
    {
        x = FMath::Clamp(x, 0, Chunk.Resolution - 1);
        y = FMath::Clamp(y, 0, Chunk.Resolution - 1);
        z = FMath::Clamp(z, 0, Chunk.Resolution - 1);
        return Chunk.At(x, y, z).Density;
    };
    return FVector3f(
        D(X + 1, Y,     Z    ) - D(X - 1, Y,     Z    ),
        D(X,     Y + 1, Z    ) - D(X,     Y - 1, Z    ),
        D(X,     Y,     Z + 1) - D(X,     Y,     Z - 1)
    );
    // no /2*VoxelSize: we only need direction, not magnitude
}

// ---------------------------------------------------------------------------
// Solve3x3 — Gaussian elimination with partial pivoting
// ---------------------------------------------------------------------------
bool FVoxelDualContouring::Solve3x3(float A[3][3], float b[3], float out[3])
{
    float M[3][4] = {
        { A[0][0], A[0][1], A[0][2], b[0] },
        { A[1][0], A[1][1], A[1][2], b[1] },
        { A[2][0], A[2][1], A[2][2], b[2] }
    };

    for (int32 col = 0; col < 3; ++col)
    {
        // find pivot
        int32 pivot = col;
        for (int32 r = col + 1; r < 3; ++r)
            if (FMath::Abs(M[r][col]) > FMath::Abs(M[pivot][col]))
                pivot = r;

        if (pivot != col)
            for (int32 c = 0; c <= 3; ++c)
                Swap(M[col][c], M[pivot][c]);

        if (FMath::Abs(M[col][col]) < 1e-8f)
            return false;

        for (int32 r = col + 1; r < 3; ++r)
        {
            float f = M[r][col] / M[col][col];
            for (int32 c = col; c <= 3; ++c)
                M[r][c] -= f * M[col][c];
        }
    }

    // back-substitution
    for (int32 i = 2; i >= 0; --i)
    {
        out[i] = M[i][3];
        for (int32 j = i + 1; j < 3; ++j)
            out[i] -= M[i][j] * out[j];
        out[i] /= M[i][i];
    }
    return true;
}

// ---------------------------------------------------------------------------
// ExtractSurface
// ---------------------------------------------------------------------------
FVoxelMeshData FVoxelDualContouring::ExtractSurface(const FVoxelChunk& Chunk)
{
    FVoxelMeshData OutMesh;

    const int32   ExtR      = Chunk.Resolution;            // CanonicalR + 2*B
    const int32   B         = Chunk.BorderSize;
    const int32   R         = ExtR - 2 * B;                // canonical cell count per axis
    const float   VoxelSize = Chunk.VoxelSize;

    if (R <= 0)
        return OutMesh;

    // -----------------------------------------------------------------------
    // Pass 1 — compute one vertex per active cell via QEF
    // -----------------------------------------------------------------------
    // CellToVertex maps cell coord (in extended grid) to vertex index in OutMesh
    TMap<FIntVector, int32> CellToVertex;
    CellToVertex.Reserve(R * R * R / 4);

    // Scratch arrays — reused per cell
    float   D[8];
    FVector WorldPos[8];

    // Iterate [0, R] inclusive: cells at cx/cy/cz=R straddle the +boundary and
    // provide the vertices needed for sign-changing edges at the chunk's far face.
    for (int32 cz = 0; cz <= R; ++cz)
    for (int32 cy = 0; cy <= R; ++cy)
    for (int32 cx = 0; cx <= R; ++cx)
    {
        // Read 8 corner densities
        for (int32 i = 0; i < 8; ++i)
        {
            const int32 gx = cx + GCornerDX[i];
            const int32 gy = cy + GCornerDY[i];
            const int32 gz = cz + GCornerDZ[i];
            D[i] = Chunk.At(gx, gy, gz).Density;
            WorldPos[i] = FVector(
                static_cast<double>(gx) * VoxelSize,
                static_cast<double>(gy) * VoxelSize,
                static_cast<double>(gz) * VoxelSize
            );
        }

        // Count solid corners
        int32 SolidCount = 0;
        for (int32 i = 0; i < 8; ++i)
            if (D[i] > 0.f) ++SolidCount;

        if (SolidCount == 0 || SolidCount == 8)
            continue; // no surface crossing

        // Accumulate QEF
        FQEFData QEF;

        for (int32 e = 0; e < 12; ++e)
        {
            const int32 c0 = GEdgeC0[e];
            const int32 c1 = GEdgeC1[e];
            const float d0 = D[c0];
            const float d1 = D[c1];

            // sign change?
            if ((d0 > 0.f) == (d1 > 0.f))
                continue;

            const float denom = d0 - d1;
            if (FMath::Abs(denom) < 1e-10f)
                continue;

            const float t = d0 / denom;
            const FVector P = FMath::Lerp(WorldPos[c0], WorldPos[c1], static_cast<double>(t));

            // Gradient via lerp of per-corner central differences
            const int32 gx0 = cx + GCornerDX[c0], gy0 = cy + GCornerDY[c0], gz0 = cz + GCornerDZ[c0];
            const int32 gx1 = cx + GCornerDX[c1], gy1 = cy + GCornerDY[c1], gz1 = cz + GCornerDZ[c1];
            const FVector3f G0 = GradientAt(Chunk, gx0, gy0, gz0);
            const FVector3f G1 = GradientAt(Chunk, gx1, gy1, gz1);
            FVector3f N = FMath::Lerp(G0, G1, t);

            const float Len = N.Length();
            if (Len < 1e-6f)
                continue;
            N /= Len;

            const float nx = N.X, ny = N.Y, nz = N.Z;
            const float dot = static_cast<float>(nx * P.X + ny * P.Y + nz * P.Z);

            QEF.ATA[0] += nx * nx;
            QEF.ATA[1] += nx * ny;
            QEF.ATA[2] += nx * nz;
            QEF.ATA[3] += ny * ny;
            QEF.ATA[4] += ny * nz;
            QEF.ATA[5] += nz * nz;
            QEF.ATb[0] += nx * dot;
            QEF.ATb[1] += ny * dot;
            QEF.ATb[2] += nz * dot;
            QEF.PointAccum += FVector3f(static_cast<float>(P.X),
                                        static_cast<float>(P.Y),
                                        static_cast<float>(P.Z));
            ++QEF.Count;
        }

        if (QEF.Count == 0)
            continue;

        // Mass point — used as QEF center and solver fallback
        const FVector3f MassPoint = QEF.PointAccum / static_cast<float>(QEF.Count);

        // Regularization (Tikhonov) — pulls toward MassPoint, not world origin
        QEF.ATA[0] += 1e-4f;
        QEF.ATA[3] += 1e-4f;
        QEF.ATA[5] += 1e-4f;

        // Build full 3×3 from symmetric storage
        float A33[3][3] = {
            { QEF.ATA[0], QEF.ATA[1], QEF.ATA[2] },
            { QEF.ATA[1], QEF.ATA[3], QEF.ATA[4] },
            { QEF.ATA[2], QEF.ATA[4], QEF.ATA[5] }
        };

        // Center the RHS on the mass point so regularization biases toward the
        // surface centroid rather than world origin.
        float bVec[3] = {
            QEF.ATb[0] - (QEF.ATA[0]*MassPoint.X + QEF.ATA[1]*MassPoint.Y + QEF.ATA[2]*MassPoint.Z),
            QEF.ATb[1] - (QEF.ATA[1]*MassPoint.X + QEF.ATA[3]*MassPoint.Y + QEF.ATA[4]*MassPoint.Z),
            QEF.ATb[2] - (QEF.ATA[2]*MassPoint.X + QEF.ATA[4]*MassPoint.Y + QEF.ATA[5]*MassPoint.Z),
        };
        float sol[3] = {};

        FVector3f Vertex;
        if (Solve3x3(A33, bVec, sol))
        {
            Vertex = MassPoint + FVector3f(sol[0], sol[1], sol[2]);
        }
        else
        {
            Vertex = MassPoint;
        }

        // Clamp to cell AABB (local space — no origin offset, same convention as MC)
        const FVector3f CellMin = FVector3f(
            static_cast<float>(cx * VoxelSize),
            static_cast<float>(cy * VoxelSize),
            static_cast<float>(cz * VoxelSize)
        );
        const FVector3f CellMax = CellMin + FVector3f(VoxelSize, VoxelSize, VoxelSize);
        Vertex = FVector3f(
            FMath::Clamp(Vertex.X, CellMin.X, CellMax.X),
            FMath::Clamp(Vertex.Y, CellMin.Y, CellMax.Y),
            FMath::Clamp(Vertex.Z, CellMin.Z, CellMax.Z)
        );

        // Material: dominant MaterialHash from solid corners
        {
            TMap<uint16, int32> MatCount;
            for (int32 i = 0; i < 8; ++i)
            {
                const int32 gx = cx + GCornerDX[i], gy = cy + GCornerDY[i], gz = cz + GCornerDZ[i];
                if (D[i] > 0.f)
                {
                    const uint16 Mat = Chunk.At(gx, gy, gz).MaterialHash;
                    MatCount.FindOrAdd(Mat) += 1;
                }
            }
            uint16 BestMat  = 0;
            int32  BestCnt  = -1;
            for (auto& Pair : MatCount)
                if (Pair.Value > BestCnt) { BestCnt = Pair.Value; BestMat = Pair.Key; }
            QEF.MaterialHash = BestMat;
        }

        // UV: trilinear blend using (vertex - cellMin) / VoxelSize as UVW weights
        {
            const FVector3f UVW = (Vertex - CellMin) / VoxelSize;
            const float     u   = FMath::Clamp(UVW.X, 0.f, 1.f);
            const float     v   = FMath::Clamp(UVW.Y, 0.f, 1.f);
            const float     w   = FMath::Clamp(UVW.Z, 0.f, 1.f);

            FVector2f BlendedUV = FVector2f::ZeroVector;
            for (int32 i = 0; i < 8; ++i)
            {
                const int32 gx = cx + GCornerDX[i], gy = cy + GCornerDY[i], gz = cz + GCornerDZ[i];
                const FVector2f CornerUV = Chunk.At(gx, gy, gz).UV;

                const float wu = (GCornerDX[i] == 0) ? (1.f - u) : u;
                const float wv = (GCornerDY[i] == 0) ? (1.f - v) : v;
                const float ww = (GCornerDZ[i] == 0) ? (1.f - w) : w;
                BlendedUV += CornerUV * (wu * wv * ww);
            }
            QEF.UV = BlendedUV;
        }

        // Store vertex
        const int32 VertIdx = OutMesh.Vertices.Num();
        OutMesh.Vertices.Add(Vertex);
        OutMesh.UVs.Add(QEF.UV);

        CellToVertex.Add(FIntVector(cx, cy, cz), VertIdx);
        // Also store MaterialHash per vertex for triangle emission below.
        // We'll use a parallel array; reuse TriangleMaterialHashes after pass 2.
        // Temporarily store in a scratch array:
        OutMesh.TriangleMaterialHashes.Add(QEF.MaterialHash);
    }

    // TriangleMaterialHashes currently holds per-vertex material — will be rebuilt in pass 2.
    TArray<uint16> VertexMaterial = MoveTemp(OutMesh.TriangleMaterialHashes);
    OutMesh.TriangleMaterialHashes.Reset();

    // -----------------------------------------------------------------------
    // Pass 2 — emit dual quads for every sign-changing edge
    // -----------------------------------------------------------------------
    //  Axis 0 = X, Axis 1 = Y, Axis 2 = Z
    //  For each axis-aligned edge we find the 4 cells sharing it and emit a quad.

    // Cell offset pairs for the 4 cells sharing an axis edge, in (dy,dz), (dx,dz), (dx,dy) order
    // For X-axis edge at (ex,ey,ez): cells at (ex, ey+oY, ez+oZ) for all sign combos
    static const int32 GCellOffsets[3][4][3] = {
        // X-axis: 4 cells differ in Y and Z
        { { 0, -1, -1 }, { 0,  0, -1 }, { 0, -1,  0 }, { 0,  0,  0 } },
        // Y-axis: 4 cells differ in X and Z
        { { -1, 0, -1 }, {  0, 0, -1 }, { -1, 0,  0 }, {  0, 0,  0 } },
        // Z-axis: 4 cells differ in X and Y
        { { -1, -1, 0 }, {  0, -1,  0 }, { -1,  0, 0 }, {  0,  0,  0 } },
    };

    // Quad winding for each axis when the A-side (lower index) is solid
    // Index into the 4-cell array above: quad corners in CCW order (as 2 tris)
    // We use consistent winding: [0,1,2,3] where 0,1,2 and 0,2,3 form the tris
    // When A is solid the outward normal faces B; reverse if B is solid.
    // Standard winding (A solid):  tri0=[0,2,1], tri1=[0,3,2]
    // Reversed  winding (B solid): tri0=[0,1,2], tri1=[0,2,3]

    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        // Iterate over all edges along this axis
        // The edge goes from grid point (ex,ey,ez) to (ex+dx,ey+dy,ez+dz)
        // where only the Axis component increments.
        // Valid ranges: axis dimension [0, ExtR-2], other dims [0, ExtR-1]
        const int32 MaxA  = ExtR - 1; // edge start can be 0..ExtR-2 along axis
        const int32 MaxOA = ExtR - B; // edge start B..ExtR-B-1 on other axes (cells need offsets ±1)

        for (int32 eA = 0; eA < MaxA; ++eA)
        for (int32 eB = B; eB < MaxOA; ++eB)
        for (int32 eC = B; eC < MaxOA; ++eC)
        {
            // Map (eA, eB, eC) to (ex, ey, ez) based on axis
            int32 ex, ey, ez;
            if      (Axis == 0) { ex = eA; ey = eB; ez = eC; }
            else if (Axis == 1) { ex = eB; ey = eA; ez = eC; }
            else                { ex = eB; ey = eC; ez = eA; }

            const float dA = Chunk.At(ex, ey, ez).Density;
            int32 ex2 = ex, ey2 = ey, ez2 = ez;
            if      (Axis == 0) ex2++;
            else if (Axis == 1) ey2++;
            else                ez2++;
            const float dB = Chunk.At(ex2, ey2, ez2).Density;

            // Sign change?
            if ((dA > 0.f) == (dB > 0.f))
                continue;

            // Look up the 4 sharing cells
            int32 CellVerts[4];
            bool  bAllFound = true;
            for (int32 q = 0; q < 4; ++q)
            {
                int32 cx, cy, cz;
                if (Axis == 0)
                {
                    cx = ex + GCellOffsets[0][q][0];
                    cy = ey + GCellOffsets[0][q][1];
                    cz = ez + GCellOffsets[0][q][2];
                }
                else if (Axis == 1)
                {
                    cx = ex + GCellOffsets[1][q][0];
                    cy = ey + GCellOffsets[1][q][1];
                    cz = ez + GCellOffsets[1][q][2];
                }
                else
                {
                    cx = ex + GCellOffsets[2][q][0];
                    cy = ey + GCellOffsets[2][q][1];
                    cz = ez + GCellOffsets[2][q][2];
                }

                const int32* Found = CellToVertex.Find(FIntVector(cx, cy, cz));
                if (!Found)
                {
                    bAllFound = false;
                    break;
                }
                CellVerts[q] = *Found;
            }

            if (!bAllFound)
                continue;

            // Emit quad as 2 triangles; winding depends on which side is solid.
            // GCellOffsets stores cells in order: q0=(--), q1=(+-), q2=(-+), q3=(++)
            // in the plane perpendicular to the current axis.
            // CCW cycle from the +axis direction: q0 → q1 → q3 → q2.
            // Triangulation: (0,1,3) and (0,3,2) gives outward normal when A (lower) is solid.
            // Y-axis edges (Axis==1) have opposite cross-product handedness — flip winding.
            const bool bASolid = (dA > 0.f);
            // CCW from +Axis: (0,1,3),(0,3,2) when A is solid on X and Z axes.
            // Y-axis flips handedness in Unreal's left-handed coord system.
            const bool bUseAWinding = bASolid == (Axis == 1);
            const uint16 MatHash = VertexMaterial.IsValidIndex(CellVerts[0]) ? VertexMaterial[CellVerts[0]] : 0;

            if (bUseAWinding)
            {
                // CCW from +axis: (0,1,3) and (0,3,2)
                OutMesh.Triangles.Add(CellVerts[0]);
                OutMesh.Triangles.Add(CellVerts[1]);
                OutMesh.Triangles.Add(CellVerts[3]);
                OutMesh.TriangleMaterialHashes.Add(MatHash);
                OutMesh.Triangles.Add(CellVerts[0]);
                OutMesh.Triangles.Add(CellVerts[3]);
                OutMesh.Triangles.Add(CellVerts[2]);
                OutMesh.TriangleMaterialHashes.Add(MatHash);
            }
            else
            {
                // CW from +axis (CCW from -axis): (0,3,1) and (0,2,3)
                OutMesh.Triangles.Add(CellVerts[0]);
                OutMesh.Triangles.Add(CellVerts[3]);
                OutMesh.Triangles.Add(CellVerts[1]);
                OutMesh.TriangleMaterialHashes.Add(MatHash);
                OutMesh.Triangles.Add(CellVerts[0]);
                OutMesh.Triangles.Add(CellVerts[2]);
                OutMesh.Triangles.Add(CellVerts[3]);
                OutMesh.TriangleMaterialHashes.Add(MatHash);
            }
        }
    }

    return OutMesh;
}
