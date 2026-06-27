// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "Simulation/LandscapeSimTask.h"

#include "Settings/VoxelDeveloperSettings.h"
#include "Utility/Math/RecallMathUtils.h"

namespace
{
	float GetWaterLevel(const TArray<float>& SurfaceZ, const TArray<FLandscapeCell>& Cells, const int32 Index)
	{
		return SurfaceZ[Index] + Cells[Index].Depth;
	}

	float GetWaterLevelDelta(const TArray<float>& SurfaceZ, const TArray<FLandscapeCell>& Cells, const int32 SourceIndex, const int32 TargetIndex)
	{
		return GetWaterLevel(SurfaceZ, Cells, SourceIndex) - GetWaterLevel(SurfaceZ, Cells, TargetIndex);
	}
}

void FLandscapeSimTask::DoTask(ENamedThreads::Type, const FGraphEventRef&)
{
    Output->Cells             = StepLayer(Input.Cells, Input.SurfaceZ, Input.Params, Input.GridSize, Input.bUseGhostBorder);
    Output->LayerName         = Input.LayerName;
    Output->ChunkCoord        = Input.ChunkCoord;
    Output->SurfaceLayerIndex = Input.SurfaceLayerIndex;
}

/*static*/ TArray<FLandscapeCell> FLandscapeSimTask::StepLayer(
    const TArray<FLandscapeCell>& Cells,
    const TArray<float>&          SurfaceZ,
    const FLandscapeLayerParams&  Params,
    const int32                   GridSize,
    const bool                    bUseGhostBorder)
{
    const int32 CellCount = GridSize * GridSize;
    
    TArray<FLandscapeCell> Out = Cells;
    
    TArray<float> TotalOutflow;
    TotalOutflow.Init(0.f, CellCount);

    TArray<float> FlatFlowMap;
    FlatFlowMap.Init(0.f, CellCount * 4);

    TArray<float> TotalInflow;
    TotalInflow.Init(0.f, CellCount);

    TArray<FVector2f> IncomingMomentum;
    IncomingMomentum.Init(FVector2f::ZeroVector, CellCount);

    const int32 Dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    const FVector2f DirVectors[4] =
    {
        FVector2f(1.f, 0.f),
        FVector2f(-1.f, 0.f),
        FVector2f(0.f, 1.f),
        FVector2f(0.f, -1.f)
    };
    const float Acceleration = FMath::Max(0.f, Recall::Math::Utils::UnitsPerSecondSquaredToPerFrameSquared(Params.Acceleration));
    const float MaxSpeed = FMath::Max(0.f, Recall::Math::Utils::UnitsPerSecondToPerFrame(Params.MaxSpeed));
    const float Viscosity = FMath::Clamp(Params.Viscosity, 0.f, 1.f);
    const float CellSize = FMath::Max(1.f, GetDefault<UVoxelDeveloperSettings>()->VoxelSize);

    // First pass: calculate bounded equalization flows from each source cell.
    for (int32 cy = 0; cy < GridSize; ++cy)
    {
        for (int32 cx = 0; cx < GridSize; ++cx)
        {
            // Ghost borders mirror neighbor chunk state. Let interior cells flow into them so
            // CommitBorderTransfers can move only the delta, but never source flow from them.
            if (bUseGhostBorder && (cx == 0 || cx == GridSize - 1 || cy == 0 || cy == GridSize - 1))
            {
                continue;
            }

            const int32 Idx   = cx + cy * GridSize;
            const float MyDepth = Cells[Idx].Depth;
            if (MyDepth <= 0.f)
            {
                Out[Idx].Velocity = FVector2f::ZeroVector;
                continue;
            }

            Out[Idx].Velocity = Cells[Idx].Velocity * Viscosity;
            if (MaxSpeed > 0.f)
            {
                Out[Idx].Velocity = Out[Idx].Velocity.GetClampedToMaxSize(MaxSpeed);
            }
            else
            {
                Out[Idx].Velocity = FVector2f::ZeroVector;
            }

            const float MyLevel = GetWaterLevel(SurfaceZ, Cells, Idx);

            int32 LowerIndices[4];
            int32 LowerCount = 0;
            FVector2f SurfaceGradient = FVector2f::ZeroVector;
            float MaxLevelDelta = 0.f;

            for (int32 d = 0; d < 4; ++d)
            {
                const int32 nx = cx + Dirs[d][0];
                const int32 ny = cy + Dirs[d][1];
                if (nx < 0 || nx >= GridSize || ny < 0 || ny >= GridSize) continue;

                const int32 NIdx = nx + ny * GridSize;
                const float LevelDelta = GetWaterLevelDelta(SurfaceZ, Cells, Idx, NIdx);

                if (LevelDelta <= Params.SlopeThreshold) continue;
                SurfaceGradient += DirVectors[d] * (LevelDelta / CellSize);
                MaxLevelDelta = FMath::Max(MaxLevelDelta, LevelDelta);
                LowerIndices[LowerCount++] = d;
            }

            if (LowerCount == 0) continue;

            int32 Participants[5];
            int32 ParticipantCount = 1;
            Participants[0] = Idx;
            for (int32 i = 0; i < LowerCount; ++i)
            {
                const int32 d = LowerIndices[i];
                const int32 nx = cx + Dirs[d][0];
                const int32 ny = cy + Dirs[d][1];
                Participants[ParticipantCount++] = nx + ny * GridSize;
            }

            bool bActive[5] = { true, true, true, true, true };
            float TargetLevel = MyLevel;

            float TotalParticipantDepth = 0.f;
            for (int32 i = 0; i < ParticipantCount; ++i)
            {
                TotalParticipantDepth += Cells[Participants[i]].Depth;
            }

            for (;;)
            {
                float TotalSurface = 0.f;
                int32 ActiveCount = 0;

                for (int32 i = 0; i < ParticipantCount; ++i)
                {
                    if (!bActive[i]) continue;
                    const int32 PIdx = Participants[i];
                    TotalSurface += SurfaceZ[PIdx];
                    ++ActiveCount;
                }

                if (ActiveCount <= 0) break;

                TargetLevel = (TotalParticipantDepth + TotalSurface) / ActiveCount;

                bool bRemovedDryCell = false;
                for (int32 i = 0; i < ParticipantCount; ++i)
                {
                    if (!bActive[i]) continue;
                    if (TargetLevel < SurfaceZ[Participants[i]])
                    {
                        bActive[i] = false;
                        bRemovedDryCell = true;
                    }
                }

                if (!bRemovedDryCell) break;
            }

            float DesiredOutflow = 0.f;
            for (int32 i = 0; i < LowerCount; ++i)
            {
                const int32 d = LowerIndices[i];
                const int32 nx = cx + Dirs[d][0];
                const int32 ny = cy + Dirs[d][1];
                const int32 NIdx = nx + ny * GridSize;

                const float TargetDepth = FMath::Max(0.f, TargetLevel - SurfaceZ[NIdx]);
                const float DesiredFlow = FMath::Max(0.f, TargetDepth - Cells[NIdx].Depth);
                FlatFlowMap[Idx * 4 + d] = DesiredFlow;
                DesiredOutflow += DesiredFlow;
            }

            if (DesiredOutflow <= 0.f) continue;

            FVector2f NewVelocity = Cells[Idx].Velocity;
            if (SurfaceGradient.SizeSquared() > UE_SMALL_NUMBER)
            {
                NewVelocity += SurfaceGradient * Acceleration;
            }
            NewVelocity *= Viscosity;
            if (MaxSpeed > 0.f)
            {
                NewVelocity = NewVelocity.GetClampedToMaxSize(MaxSpeed);
            }
            else
            {
                NewVelocity = FVector2f::ZeroVector;
            }
            Out[Idx].Velocity = NewVelocity;

            // A single 2D velocity cannot represent perfectly radial spreading, so keep the
            // equalization pressure as a scalar flow floor and let velocity add persistence.
            const float DepthScale = MyDepth / CellSize;
            const float PressureAcceleration = Acceleration * FMath::Max(1.f, MaxLevelDelta / CellSize);
            const float AcceleratedFlow = DesiredOutflow * FMath::Clamp(PressureAcceleration * Viscosity, 0.f, 1.f);
            const float VelocityFlow = NewVelocity.Size() * DepthScale;
            const float MaxStepFlow = MaxSpeed > 0.f ? MaxSpeed * DepthScale : 0.f;
            const float MaxOutflow = FMath::Min(MyDepth, FMath::Min(DesiredOutflow, FMath::Min(FMath::Max(AcceleratedFlow, VelocityFlow), MaxStepFlow)));
            const float Scalar = MaxOutflow / DesiredOutflow;

            for (int32 i = 0; i < LowerCount; ++i)
            {
                const int32 d = LowerIndices[i];
                FlatFlowMap[Idx * 4 + d] *= Scalar;
                TotalOutflow[Idx] += FlatFlowMap[Idx * 4 + d];
            }
        }
    }

    // Second pass: apply flows with conservation check
    for (int32 cy = 0; cy < GridSize; ++cy)
    {
        for (int32 cx = 0; cx < GridSize; ++cx)
        {
            const int32 Idx      = cx + cy * GridSize;
            const float TotalOut = TotalOutflow[Idx];
            if (TotalOut <= 0.f) continue;

            const float MyDepth  = Cells[Idx].Depth;
            const float Scalar = (TotalOut > MyDepth) ? (MyDepth / TotalOut) : 1.f;

            for (int32 d = 0; d < 4; ++d)
            {
                const float Flow = FlatFlowMap[Idx * 4 + d] * Scalar;
                if (Flow <= 0.f) continue;

                const int32 nx = cx + Dirs[d][0];
                const int32 ny = cy + Dirs[d][1];
                const int32 NIdx = nx + ny * GridSize;

                Out[Idx].Depth  -= Flow;
                Out[NIdx].Depth += Flow;
                TotalInflow[NIdx] += Flow;
                IncomingMomentum[NIdx] += Out[Idx].Velocity * Flow;
            }
        }
    }

    // Clamp negatives just in case (e.g., floating-point precision errors)
    for (int32 Index = 0; Index < Out.Num(); ++Index)
    {
        FLandscapeCell& Cell = Out[Index];
        Cell.Depth = FMath::Max(Cell.Depth, 0.f);
        if (TotalInflow.IsValidIndex(Index) && TotalInflow[Index] > 0.f && Cell.Depth > UE_SMALL_NUMBER)
        {
            const float ExistingDepth = FMath::Max(0.f, Cell.Depth - TotalInflow[Index]);
            Cell.Velocity = (Cell.Velocity * ExistingDepth + IncomingMomentum[Index]) / Cell.Depth;
            if (MaxSpeed > 0.f)
            {
                Cell.Velocity = Cell.Velocity.GetClampedToMaxSize(MaxSpeed);
            }
        }
        if (Cell.Depth <= UE_SMALL_NUMBER)
        {
            Cell.Velocity = FVector2f::ZeroVector;
        }
    }

    return Out;
}
