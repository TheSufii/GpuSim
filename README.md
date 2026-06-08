# GpuSim

A GPU-driven particle system, extracted from DawnEngine as a standalone library
plus an authoring testbed. ~1M particles, fully GPU-resident: spawn → update →
lifetime → optional collision → radix sort → indirect billboard render.

The library is **engine-agnostic**. It needs only an OpenGL 4.5 context, `glm`,
and `glad`; it consumes an `EmitterDesc` (a trivially-copyable POD) and raw
texture pixels, and knows nothing about any ECS, asset system, or GUIDs. Drop it
into your own engine, or use **GpuSimLab** to author `.emitter` files and consume
those at runtime.

## Layout

| Path                | What                                                         |
|---------------------|-------------------------------------------------------------|
| `include/gpusim/`   | Public headers (`ParticleWorld`, `EmitterDesc`, …)          |
| `src/`              | Library implementation                                      |
| `shaders/particles/`| Compute + billboard GLSL (`common.glsl` mirrors CPU structs)|
| `lab/`              | GpuSimLab — SDL3 + ImGui testbed                            |
| `third_party/`      | Vendored deps: SDL3, glad, glm, ImGui, nlohmann/json, stb   |

## Build (Ninja + MSVC)

```sh
cmake -B build -G Ninja
cmake --build build --target GpuSim      # library only — foundation check
cmake --build build --target GpuSimLab   # the testbed
```

## Using the library

```cpp
#include <gpusim/ParticleWorld.h>

ParticleWorld world;
world.Initialize(1u << 20, "shaders/particles/");   // 1,048,576 particles
world.SetupDefault(myBillboardProgram);             // you compile the billboard shader
world.SetCamera(view, proj);                         // each frame
world.CreateEmitter(desc);                           // desc = an EmitterDesc

world.Dispatch(dt);   // run the compute pipeline
world.Render();       // billboard pass (self-contained GL state)
```

A host needs only: a GL 4.5 context, a camera, optionally a depth texture
(`SetDepthTexture`) for soft particles / collision, and raw pixels for
`CreateTextureArray`. No lighting, materials, or scene renderer required.
