# VoxelPlugin

VoxelPlugin is an Unreal Engine C++ library for modifier-driven voxel worlds, surface extraction, streaming, rendering, and landscape-layer simulation.

The plugin provides the data structures, generators, and world subsystems needed to build these systems, but it does not impose a gameplay framework or automatically run the complete pipeline. A host project is expected to register views and modifiers, decide when work should run, and explicitly coordinate generation and flush points.

> [!WARNING]
> VoxelPlugin is experimental software developed through a small prototype. It is intended for experimentation and as a reference or foundation for further development, not for production use. Most generation and simulation work is multithreaded, but performance and scalability have not yet been validated for large worlds or production workloads.

## Core model

Voxel data is generated from modifiers rather than edited directly. A modifier describes a shape, transform, operation, surface type, and material. Adding or removing one marks the affected chunks dirty; those chunks can then be rebuilt asynchronously.

Supported modifier operations are additive fill and subtractive carving. Current shape support includes spheres, boxes, and signed-distance fields baked from static meshes. Generated chunks can be meshed with Marching Cubes, Greedy Meshing, or Dual Contouring.

The main data flow is:

```text
Modifiers
  |---> voxel chunk generation ---> voxel meshes
  `---> surface extraction -------> landscape layers ---> landscape meshes
```

Surface extraction scans the modifier field independently of baked voxel meshes. It produces a chunked 2D representation of walkable floors, including multiple vertical floors for caves or overhangs. Landscape simulation uses this surface representation as its terrain input. Other systems can consume the same data; for example, it could support navigation-mesh generation in the future.

## Modules

- `VoxelCore` contains voxel, modifier, grid, material registry, and mesh-algorithm types.
- `VoxelStreaming` manages views, dirty chunks, asynchronous chunk baking, mesh SDF baking, and modifier convenience functions.
- `VoxelSurface` extracts and stores floor heights and normals from modifiers.
- `VoxelLandscape` stores and simulates named landscape layers, including configurable fluid layers.
- `VoxelRender` generates and displays voxel chunk meshes.
- `VoxelLandscapeRender` generates landscape meshes and groups them into procedural mesh actors.
- `VoxelEditor` contains editor integration.
- `VoxelTestSuite` contains Unreal Automation tests and is excluded from cooked builds.

## Integration

Enable `VoxelPlugin` in the host project's `.uproject`, then add only the modules used by your game module to its `Build.cs` dependencies. Most runtime entry points are available through Unreal world subsystems.

The exact frame schedule belongs to the host project. A typical update is:

1. Register or update one or more `FVoxelView` instances on `UVoxelStreamingSubsystem`.
2. Add, update, or remove modifiers on the game thread.
3. Call `UVoxelStreamingSubsystem::Tick` to update the desired streamed chunk set.
4. Start dirty voxel generation with `StartDirtyChunkGeneration`.
5. Start surface generation with `UVoxelSurfaceSubsystem::StartSurfaceGeneration`, using the dirty voxel chunk coordinates.
6. Call `ForceEndGeneration`, then notify the render system of the baked chunks.
7. Call `ForceEndSurfaceGeneration` before starting systems that consume the updated surface.
8. Run landscape simulation with `StartSimulation` and `ForceEndSimulation` when landscape layers are in use.

Do not mutate modifiers while voxel generation is active. `StartGeneration` calls are non-blocking; their matching `ForceEnd...` calls are synchronization points and must complete before starting another generation pass on the same generator.

### Adding a modifier

`Voxel::Modifier::Utils` provides the simplest game-thread API. Primitive modifiers use the owner's transform; mesh SDF modifiers are baked from the owner's static mesh component.

```cpp
#include "Modifier/VoxelModifierUtils.h"

FVoxelModifierParameters Parameters;
Parameters.Type = EModifierType::PrimitiveSphere;
Parameters.Operation = EModifierOp::Add;
Parameters.SurfaceType = 0;

FModifierHandle Handle = Voxel::Modifier::Utils::AddModifier(OwnerActor, Parameters);

// Later, on the game thread:
Voxel::Modifier::Utils::RemoveModifier(WorldContextObject, Handle);
```

For procedural inputs, construct `FVoxelModifierData` and use `AddModifierWithData`. Lower-level code can work directly with `FVoxelGrid` when subsystem ownership is not appropriate.

## Configuration

Project-wide settings are available under **Project Settings > Plugins > Voxel Plugin**. They include:

- voxel size, chunk resolution, and chunk border size;
- static-mesh SDF bake resolution;
- voxel mesh algorithm and chunk actor pool size;
- minimum separation between extracted surface floors;
- procedural landscape mesh grouping;
- landscape layer materials and fluid simulation parameters.

Settings take effect on the next editor session or game start.

## Tests

From the project root, run:

```bat
.\Plugins\VoxelPlugin\Scripts\RunUnitTests.bat
```

The suite covers grids and modifiers, chunk generation, mesh algorithms, streaming, surface extraction, landscape simulation, and landscape mesh generation. Test reports are written to the project's `Reports/VoxelPlugin` directory.

## License

VoxelPlugin is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
