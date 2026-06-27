// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Simulation/LandscapeSimTask.h"
#include "VoxelLandscapeTypes.h"

namespace
{
	float SumDepth(const TArray<FLandscapeCell>& Cells)
	{
		float Sum = 0.f;
		for (const FLandscapeCell& Cell : Cells)
		{
			Sum += Cell.Depth;
		}
		return Sum;
	}

	bool IsNearlyEqual(const FVector2f& A, const FVector2f& B, const float Tolerance = 0.01f)
	{
		return (A - B).Size() <= Tolerance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_FlowsDownhill,
	"VoxelPlugin.Landscape.Sim.FlowsDownhill",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_FlowsDownhill::RunTest(const FString& Parameters)
{
	// 3×3 grid (no border for simplicity), cell (1,1) has water, (1,0) is lower.
	// Surface at (1,1) Z=100, surface at (1,0) Z=50 → water should flow toward (1,0).
	const int32 Size = 3;

	TArray<FLandscapeCell> Cells;
	Cells.SetNum(Size * Size);
	Cells[1 + 1 * Size].Depth = 10.f;  // (cx=1, cy=1) has water

	TArray<float> SurfaceZ;
	SurfaceZ.SetNum(Size * Size);
	SurfaceZ[1 + 0 * Size] = 50.f;   // (1,0) lower
	SurfaceZ[1 + 1 * Size] = 100.f;  // (1,1) higher
	SurfaceZ[1 + 2 * Size] = 100.f;
	SurfaceZ[0 + 1 * Size] = 100.f;
	SurfaceZ[2 + 1 * Size] = 100.f;

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 0.01f;
	Params.Acceleration   = 1800.f;
	Params.MaxSpeed       = 300.f;
	Params.Viscosity      = 1.f;

	TArray<FLandscapeCell> Result = FLandscapeSimTask::StepLayer(Cells, SurfaceZ, Params, Size);

	const float SourceAfter = Result[1 + 1 * Size].Depth;
	const float TargetAfter = Result[1 + 0 * Size].Depth;

	TestTrue("Water flowed out of source cell", SourceAfter < 10.f);
	TestTrue("Water arrived in lower cell",     TargetAfter > 0.f);
	TestTrue("Source gained downhill velocity", Result[1 + 1 * Size].Velocity.Y < 0.f);
	TestTrue("Total water conserved (±0.01)",
		FMath::Abs(SumDepth(Result) - SumDepth(Cells)) < 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_NoFlowBelowThreshold,
	"VoxelPlugin.Landscape.Sim.NoFlowBelowThreshold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_NoFlowBelowThreshold::RunTest(const FString& Parameters)
{
	const int32 Size = 3;
	TArray<FLandscapeCell> Cells;
	Cells.SetNum(Size * Size);
	Cells[1 + 1 * Size].Depth = 5.f;

	// All neighbors at same height → height diff = value diff → below threshold if equal values
	TArray<float> SurfaceZ;
	SurfaceZ.SetNum(Size * Size);
	for (float& Z : SurfaceZ) Z = 100.f;

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 100.f;  // very high threshold → no flow

	TArray<FLandscapeCell> Result = FLandscapeSimTask::StepLayer(Cells, SurfaceZ, Params, Size);

	TestEqual("No flow with high threshold", Result[1 + 1 * Size].Depth, 5.f);
	TestTrue("No flow keeps velocity unchanged", IsNearlyEqual(Result[1 + 1 * Size].Velocity, FVector2f::ZeroVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_DoesNotOvershootEquilibrium,
	"VoxelPlugin.Landscape.Sim.DoesNotOvershootEquilibrium",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_DoesNotOvershootEquilibrium::RunTest(const FString& Parameters)
{
	const int32 Size = 3;
	TArray<FLandscapeCell> Cells;
	Cells.SetNum(Size * Size);
	Cells[1 + 1 * Size].Depth = 100.f;

	TArray<float> SurfaceZ;
	SurfaceZ.Init(0.f, Size * Size);

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 0.01f;
	Params.Acceleration   = 3600000.f;
	Params.MaxSpeed       = 60000.f;
	Params.Viscosity      = 1.f;

	TArray<FLandscapeCell> Result = FLandscapeSimTask::StepLayer(Cells, SurfaceZ, Params, Size);

	TestEqual("Source remains at local equilibrium depth", Result[1 + 1 * Size].Depth, 20.f);
	TestEqual("Left neighbor reaches local equilibrium depth", Result[0 + 1 * Size].Depth, 20.f);
	TestEqual("Right neighbor reaches local equilibrium depth", Result[2 + 1 * Size].Depth, 20.f);
	TestEqual("Top neighbor reaches local equilibrium depth", Result[1 + 2 * Size].Depth, 20.f);
	TestEqual("Bottom neighbor reaches local equilibrium depth", Result[1 + 0 * Size].Depth, 20.f);
	TestTrue("Total water conserved (±0.01)", FMath::Abs(SumDepth(Result) - 100.f) < 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_FlatTerrainConverges,
	"VoxelPlugin.Landscape.Sim.FlatTerrainConverges",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_FlatTerrainConverges::RunTest(const FString& Parameters)
{
	const int32 Size = 3;
	TArray<FLandscapeCell> Cells;
	Cells.SetNum(Size * Size);
	Cells[1 + 1 * Size].Depth = 90.f;

	TArray<float> SurfaceZ;
	SurfaceZ.Init(0.f, Size * Size);

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 0.001f;
	Params.Acceleration   = 1800.f;
	Params.MaxSpeed       = 60000.f;
	Params.Viscosity      = 0.8f;

	TArray<FLandscapeCell> Result = Cells;
	for (int32 Step = 0; Step < 64; ++Step)
	{
		Result = FLandscapeSimTask::StepLayer(Result, SurfaceZ, Params, Size);
	}

	float MinDepth = TNumericLimits<float>::Max();
	float MaxDepth = TNumericLimits<float>::Lowest();
	for (const FLandscapeCell& Cell : Result)
	{
		MinDepth = FMath::Min(MinDepth, Cell.Depth);
		MaxDepth = FMath::Max(MaxDepth, Cell.Depth);
	}

	TestTrue("Flat terrain approaches an even depth", MaxDepth - MinDepth < 0.25f);
	TestTrue("Total water conserved (±0.01)", FMath::Abs(SumDepth(Result) - 90.f) < 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_GhostBorderDoesNotEmitFluid,
	"VoxelPlugin.Landscape.Sim.GhostBorderDoesNotEmitFluid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_GhostBorderDoesNotEmitFluid::RunTest(const FString& Parameters)
{
	const int32 Size = 3;
	TArray<FLandscapeCell> Cells;
	Cells.SetNum(Size * Size);
	Cells[0 + 1 * Size].Depth = 100.f;

	TArray<float> SurfaceZ;
	SurfaceZ.Init(0.f, Size * Size);

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 0.001f;
	Params.Acceleration   = 3600000.f;
	Params.MaxSpeed       = 60000.f;
	Params.Viscosity      = 1.f;

	const TArray<FLandscapeCell> Result = FLandscapeSimTask::StepLayer(Cells, SurfaceZ, Params, Size, true);

	TestEqual("Copied ghost fluid stays on the border", Result[0 + 1 * Size].Depth, 100.f);
	TestEqual("Ghost border does not emit into the chunk interior", Result[1 + 1 * Size].Depth, 0.f);
	TestTrue("Total water conserved (±0.01)", FMath::Abs(SumDepth(Result) - SumDepth(Cells)) < 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_VelocityAcceleratesAcrossSteps,
	"VoxelPlugin.Landscape.Sim.VelocityAcceleratesAcrossSteps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_VelocityAcceleratesAcrossSteps::RunTest(const FString& Parameters)
{
	const int32 Size = 3;
	TArray<FLandscapeCell> Cells;
	Cells.SetNum(Size * Size);
	Cells[1 + 1 * Size].Depth = 100.f;

	TArray<float> SurfaceZ;
	SurfaceZ.Init(100.f, Size * Size);
	SurfaceZ[2 + 1 * Size] = 0.f;

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 0.01f;
	Params.Acceleration   = 3600.f;
	Params.MaxSpeed       = 600.f;
	Params.Viscosity      = 1.f;

	const TArray<FLandscapeCell> FirstStep = FLandscapeSimTask::StepLayer(Cells, SurfaceZ, Params, Size);
	const TArray<FLandscapeCell> SecondStep = FLandscapeSimTask::StepLayer(FirstStep, SurfaceZ, Params, Size);

	TestTrue("Velocity points toward the lower neighbor", FirstStep[1 + 1 * Size].Velocity.X > 0.f);
	TestTrue("Velocity accelerates while downhill pressure remains",
		SecondStep[1 + 1 * Size].Velocity.Size() > FirstStep[1 + 1 * Size].Velocity.Size());
	TestTrue("Total water conserved (±0.01)", FMath::Abs(SumDepth(SecondStep) - SumDepth(Cells)) < 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_VelocityClampsToMaxSpeed,
	"VoxelPlugin.Landscape.Sim.VelocityClampsToMaxSpeed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_VelocityClampsToMaxSpeed::RunTest(const FString& Parameters)
{
	const int32 Size = 3;
	TArray<FLandscapeCell> Cells;
	Cells.SetNum(Size * Size);
	Cells[1 + 1 * Size].Depth = 100.f;

	TArray<float> SurfaceZ;
	SurfaceZ.Init(100.f, Size * Size);
	SurfaceZ[2 + 1 * Size] = 0.f;

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 0.01f;
	Params.Acceleration   = 360000.f;
	Params.MaxSpeed       = 120.f;
	Params.Viscosity      = 1.f;

	const TArray<FLandscapeCell> Result = FLandscapeSimTask::StepLayer(Cells, SurfaceZ, Params, Size);

	TestTrue("Velocity is clamped by MaxSpeed", Result[1 + 1 * Size].Velocity.Size() <= 2.01f);
	TestTrue("Flow is limited by depth-scaled MaxSpeed", Result[1 + 1 * Size].Depth >= 90.f);
	TestTrue("Total water conserved (±0.01)", FMath::Abs(SumDepth(Result) - SumDepth(Cells)) < 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_DeepWaterTransfersMoreThanShallowWater,
	"VoxelPlugin.Landscape.Sim.DeepWaterTransfersMoreThanShallowWater",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_DeepWaterTransfersMoreThanShallowWater::RunTest(const FString& Parameters)
{
	const int32 Size = 3;

	TArray<float> SurfaceZ;
	SurfaceZ.Init(0.f, Size * Size);
	SurfaceZ[0 + 1 * Size] = 1000.f;
	SurfaceZ[1 + 0 * Size] = 1000.f;
	SurfaceZ[1 + 2 * Size] = 1000.f;

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 0.01f;
	Params.Acceleration   = 980.f;
	Params.MaxSpeed       = 100.f;
	Params.Viscosity      = 1.f;

	TArray<FLandscapeCell> ShallowCells;
	ShallowCells.SetNum(Size * Size);
	ShallowCells[1 + 1 * Size].Depth = 2.f;
	ShallowCells[2 + 1 * Size].Depth = 1.f;

	TArray<FLandscapeCell> DeepCells;
	DeepCells.SetNum(Size * Size);
	DeepCells[1 + 1 * Size].Depth = 100.f;
	DeepCells[2 + 1 * Size].Depth = 1.f;

	const TArray<FLandscapeCell> ShallowResult = FLandscapeSimTask::StepLayer(ShallowCells, SurfaceZ, Params, Size);
	const TArray<FLandscapeCell> DeepResult = FLandscapeSimTask::StepLayer(DeepCells, SurfaceZ, Params, Size);

	const float ShallowTransferred = ShallowCells[1 + 1 * Size].Depth - ShallowResult[1 + 1 * Size].Depth;
	const float DeepTransferred = DeepCells[1 + 1 * Size].Depth - DeepResult[1 + 1 * Size].Depth;

	TestTrue("Deep water head transfers more than shallow water head", DeepTransferred > ShallowTransferred * 10.f);
	TestTrue("Deep water transferred visible depth in one step", DeepTransferred > 1.f);
	TestTrue("Shallow water remains conservative", FMath::Abs(SumDepth(ShallowResult) - SumDepth(ShallowCells)) < 0.01f);
	TestTrue("Deep water remains conservative", FMath::Abs(SumDepth(DeepResult) - SumDepth(DeepCells)) < 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandscape_Sim_InteriorCanFlowToGhostBorder,
	"VoxelPlugin.Landscape.Sim.InteriorCanFlowToGhostBorder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVoxelLandscape_Sim_InteriorCanFlowToGhostBorder::RunTest(const FString& Parameters)
{
	const int32 Size = 3;
	TArray<FLandscapeCell> Cells;
	Cells.SetNum(Size * Size);
	Cells[1 + 1 * Size].Depth = 100.f;

	TArray<float> SurfaceZ;
	SurfaceZ.Init(0.f, Size * Size);

	FLandscapeLayerParams Params;
	Params.SlopeThreshold = 0.001f;
	Params.Acceleration   = 3600000.f;
	Params.MaxSpeed       = 60000.f;
	Params.Viscosity      = 1.f;

	const TArray<FLandscapeCell> Result = FLandscapeSimTask::StepLayer(Cells, SurfaceZ, Params, Size, true);

	TestTrue("Interior fluid can move into ghost borders for cross-chunk transfer", Result[0 + 1 * Size].Depth > 0.f);
	TestTrue("Total water conserved (±0.01)", FMath::Abs(SumDepth(Result) - SumDepth(Cells)) < 0.01f);

	return true;
}
