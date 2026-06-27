// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "CoreMinimal.h"
#include "Settings/VoxelDeveloperSettings.h"
#include "VoxelLandscapeTypes.h"

namespace VoxelLandscape::Utils
{
	inline int32 GetColumnResolution()
	{
		const UVoxelDeveloperSettings* Settings = GetDefault<UVoxelDeveloperSettings>();
		return Settings->ChunkResolution + 2;
	}

	inline int32 GetCellCount(const int32 ColumnResolution)
	{
		return ColumnResolution * ColumnResolution;
	}

	inline int32 GetCellIndex(const int32 X, const int32 Y, const int32 ColumnResolution)
	{
		return X + Y * ColumnResolution;
	}

	inline bool IsValidCellBuffer(const TArray<FLandscapeCell>& Cells, const int32 ColumnResolution)
	{
		return Cells.Num() == GetCellCount(ColumnResolution);
	}

	inline bool HasFluid(const TArray<FLandscapeCell>& Cells)
	{
		for (const FLandscapeCell& Cell : Cells)
		{
			if (Cell.Depth > 0.f)
			{
				return true;
			}
		}
		return false;
	}

	struct FBorderDirection
	{
		FIntVector2 Delta;
		bool bVertical = false;
		int32 ThisEdge = 0;
		int32 NeighborEdge = 0;
	};

	inline void GetBorderDirections(const int32 ColumnResolution, FBorderDirection(&OutDirections)[4])
	{
		OutDirections[0] = { FIntVector2( 1,  0), true,  ColumnResolution - 1, 1                    };
		OutDirections[1] = { FIntVector2(-1,  0), true,  0,                    ColumnResolution - 2 };
		OutDirections[2] = { FIntVector2( 0,  1), false, ColumnResolution - 1, 1                    };
		OutDirections[3] = { FIntVector2( 0, -1), false, 0,                    ColumnResolution - 2 };
	}

	inline int32 GetBorderCellIndex(const FBorderDirection& Direction, const int32 Line, const int32 ColumnResolution)
	{
		return Direction.bVertical
			? GetCellIndex(Direction.ThisEdge, Line, ColumnResolution)
			: GetCellIndex(Line, Direction.ThisEdge, ColumnResolution);
	}

	inline int32 GetNeighborBorderCellIndex(const FBorderDirection& Direction, const int32 Line, const int32 ColumnResolution)
	{
		return Direction.bVertical
			? GetCellIndex(Direction.NeighborEdge, Line, ColumnResolution)
			: GetCellIndex(Line, Direction.NeighborEdge, ColumnResolution);
	}
}
