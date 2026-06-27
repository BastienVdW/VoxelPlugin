// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Geometry/VoxelGreedyMesh.h"
#include "Geometry/VoxelDualContouring.h"
#include "Voxel/VoxelTypes.h"

// ---------------------------------------------------------------------------
// Test 1: GreedyMesh — empty chunk produces no mesh
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreedyMesh_EmptyChunk_NoMesh,
	"VoxelPlugin.MeshAlgo.GreedyMesh_EmptyChunk_NoMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGreedyMesh_EmptyChunk_NoMesh::RunTest(const FString& Parameters)
{
	FVoxelChunk Chunk;
	Chunk.Init(FIntVector(0, 0, 0), /*CanonicalResolution=*/4, /*VoxelSize=*/25.f);
	// All voxels default to Density=0 (empty)

	FVoxelMeshData Mesh = FVoxelGreedyMesh::ExtractSurface(Chunk);

	TestEqual("Empty chunk: no vertices", Mesh.Vertices.Num(), 0);
	TestEqual("Empty chunk: no triangles", Mesh.Triangles.Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Test 2: GreedyMesh — solid slab produces valid mesh
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGreedyMesh_SolidSlab_ValidMesh,
	"VoxelPlugin.MeshAlgo.GreedyMesh_SolidSlab_ValidMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FGreedyMesh_SolidSlab_ValidMesh::RunTest(const FString& Parameters)
{
	const int32 R = 4;
	const float VS = 25.f;

	FVoxelChunk Chunk;
	Chunk.Init(FIntVector(0, 0, 0), R, VS);
	const int32 B = Chunk.BorderSize;

	// Set canonical voxels at Z=B solid — single slab
	for (int32 x = B; x < Chunk.Resolution - B; ++x)
	for (int32 y = B; y < Chunk.Resolution - B; ++y)
	{
		Chunk.At(x, y, B).Density = 1.f;
	}

	FVoxelMeshData Mesh = FVoxelGreedyMesh::ExtractSurface(Chunk);

	TestTrue("Slab: has vertices", Mesh.Vertices.Num() > 0);
	TestTrue("Slab: triangle count divisible by 3", Mesh.Triangles.Num() % 3 == 0);
	TestEqual("Slab: material hash count == tri count",
		Mesh.TriangleMaterialHashes.Num(), Mesh.Triangles.Num() / 3);

	// All vertices should be within [0, R*VS] on each axis
	const float MaxCoord = static_cast<float>(R) * VS;
	for (const FVector3f& V : Mesh.Vertices)
	{
		TestTrue("Vertex X in range", V.X >= 0.f && V.X <= MaxCoord);
		TestTrue("Vertex Y in range", V.Y >= 0.f && V.Y <= MaxCoord);
		TestTrue("Vertex Z in range", V.Z >= 0.f && V.Z <= MaxCoord);
	}

	return true;
}

// ---------------------------------------------------------------------------
// Test 3: DualContouring — empty chunk produces no mesh
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContouring_EmptyChunk_NoMesh,
	"VoxelPlugin.MeshAlgo.DualContouring_EmptyChunk_NoMesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDualContouring_EmptyChunk_NoMesh::RunTest(const FString& Parameters)
{
	// 4 canonical cells with 1-voxel border → Resolution=6
	FVoxelChunk Chunk;
	Chunk.Init(FIntVector(0, 0, 0), /*CanonicalResolution=*/4, /*VoxelSize=*/25.f, /*BorderSize=*/1);
	// All voxels default to Density=0

	FVoxelMeshData Mesh = FVoxelDualContouring::ExtractSurface(Chunk);

	TestEqual("DC empty: no vertices", Mesh.Vertices.Num(), 0);
	TestEqual("DC empty: no triangles", Mesh.Triangles.Num(), 0);

	return true;
}

// ---------------------------------------------------------------------------
// Test 4: DualContouring — sphere SDF produces outward normals
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContouring_Sphere_OutwardNormals,
	"VoxelPlugin.MeshAlgo.DualContouring_Sphere_OutwardNormals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDualContouring_Sphere_OutwardNormals::RunTest(const FString& Parameters)
{
	// 8 canonical cells with 1-voxel border → Resolution=10
	const int32 CanonicalR = 8;
	const int32 Border     = 1;
	const float VS = 25.f;
	const float SphereRadius = 3.5f * VS;

	FVoxelChunk Chunk;
	Chunk.Init(FIntVector(0, 0, 0), CanonicalR, VS, Border);
	const int32 ExtRes = Chunk.Resolution; // 10

	// Sphere center in grid coords: midpoint of full extended grid
	const FVector Center(5.f * VS, 5.f * VS, 5.f * VS);

	for (int32 z = 0; z < ExtRes; ++z)
	for (int32 y = 0; y < ExtRes; ++y)
	for (int32 x = 0; x < ExtRes; ++x)
	{
		const FVector Pos(static_cast<float>(x) * VS,
		                  static_cast<float>(y) * VS,
		                  static_cast<float>(z) * VS);
		Chunk.At(x, y, z).Density = SphereRadius - static_cast<float>(FVector::Dist(Pos, Center));
	}

	FVoxelMeshData Mesh = FVoxelDualContouring::ExtractSurface(Chunk);

	TestTrue("Sphere: has vertices", Mesh.Vertices.Num() > 0);
	TestTrue("Sphere: triangle count divisible by 3", Mesh.Triangles.Num() % 3 == 0);
	TestEqual("Sphere: material hash count == tri count",
		Mesh.TriangleMaterialHashes.Num(), Mesh.Triangles.Num() / 3);

	// Sphere center in world space = ChunkOriginWorld() + local center
	// ChunkOriginWorld for chunk (0,0,0) = (0,0,0), so world center == local center
	const FVector SphereCenter(125.f, 125.f, 125.f);

	// DC emits quads as 2 tris each. The two tris per quad may have slightly diverging
	// normals on non-planar quads, so we evaluate outward orientation per-quad
	// (average normal of both tris) rather than per-triangle.
	const int32 TriCount = Mesh.Triangles.Num() / 3;
	const int32 QuadCount = TriCount / 2; // DC always emits quads
	int32 OutwardQuads = 0;
	int32 DegenerateQuads = 0;

	for (int32 Q = 0; Q < QuadCount; ++Q)
	{
		// Two triangles: T0 = Q*2, T1 = Q*2+1
		FVector QuadNormal = FVector::ZeroVector;
		FVector QuadCentroid = FVector::ZeroVector;
		int32 GoodTris = 0;

		for (int32 t = 0; t < 2; ++t)
		{
			const int32 T = Q * 2 + t;
			const int32 I0 = Mesh.Triangles[T * 3 + 0];
			const int32 I1 = Mesh.Triangles[T * 3 + 1];
			const int32 I2 = Mesh.Triangles[T * 3 + 2];

			const FVector VA(Mesh.Vertices[I0]);
			const FVector VB(Mesh.Vertices[I1]);
			const FVector VC(Mesh.Vertices[I2]);

			const FVector N = FVector::CrossProduct(VB - VA, VC - VA);
			if (!N.IsNearlyZero())
			{
				QuadNormal += N;
				QuadCentroid += (VA + VB + VC) / 3.0;
				++GoodTris;
			}
		}

		if (GoodTris == 0)
		{
			++DegenerateQuads;
			continue;
		}

		QuadCentroid /= static_cast<double>(GoodTris);
		const FVector NetNormal = QuadNormal.GetSafeNormal();
		const FVector ToCenter = (QuadCentroid - SphereCenter).GetSafeNormal();
		if (FVector::DotProduct(NetNormal, ToCenter) > 0.f)
		{
			++OutwardQuads;
		}
	}

	const int32 NonDegenerateQuads = QuadCount - DegenerateQuads;
	if (NonDegenerateQuads > 0)
	{
		const float OutwardRatio = static_cast<float>(OutwardQuads) / static_cast<float>(NonDegenerateQuads);
		TestTrue("Sphere: >= 90% outward quad normals", OutwardRatio >= 0.9f);
	}

	return true;
}
