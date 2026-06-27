// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "Simulation/LandscapeSimTypes.h"
#include "Async/TaskGraphInterfaces.h"

class VOXELLANDSCAPE_API FLandscapeSimTask
{
public:
	FLandscapeSimTask(FLandscapeSimInput InInput, FLandscapeSimOutput* OutOutput)
		: Input(MoveTemp(InInput)), Output(OutOutput) {}

	static TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FLandscapeSimTask, STATGROUP_TaskGraphTasks);
	}
	static ENamedThreads::Type   GetDesiredThread()     { return ENamedThreads::AnyBackgroundThreadNormalTask; }
	static ESubsequentsMode::Type GetSubsequentsMode()  { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type, const FGraphEventRef&);

	// Single CA step; exposed as static for unit testing without TaskGraph.
	// GridSize = number of columns per axis (includes border).
	static TArray<FLandscapeCell> StepLayer(
		const TArray<FLandscapeCell>& Cells,
		const TArray<float>&          SurfaceZ,
		const FLandscapeLayerParams&  Params,
		int32                         GridSize,
		bool                          bUseGhostBorder = false);

private:
	FLandscapeSimInput   Input;
	FLandscapeSimOutput* Output;
};
