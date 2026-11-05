# TaruGX FPGA 3D architecture

TaruGX currently exposes a Mesa loader entry point, but forwards it to
`kms_swrast`; LLVM executes all 3D drawing on the host CPU.  The target is a
native Gallium driver whose draw calls become validated DRM jobs and execute in
the FPGA without granting userspace access to physical DDR addresses.

## First hardware milestone

The first end-to-end primitive is an untextured, solid RGB565 triangle:

1. Mesa writes three screen-space vertices into a command buffer.
2. The DRM driver validates the command and referenced VRAM GEM, then attaches
   the job to the existing DRM scheduler and reservation fences.
3. The FPGA clips the primitive to the render target, applies the top-left fill
   rule, and writes covered pixels through the shared DDR arbiter.
4. MSI completes the scheduler fence.  The resulting GEM can be scanned out
   directly without a CPU copy.

Vertices use signed 16.8 fixed-point X/Y coordinates. Pixels are sampled at
their centers. The winding is normalized before rasterization, and degenerate
triangles produce no pixels. Inclusive edges use the top-left convention for a
screen whose Y coordinate increases downward: an edge is inclusive when it
points upward, or when it is horizontal and points right. The executable
golden definition is `tools/triangle_reference.py`.

## Command and trust boundary

The 3D ioctl will contain GEM handles, offsets, dimensions, vertices, state and
syncobj points—not physical addresses. The kernel must reject arithmetic
overflow, out-of-object render targets, unsupported formats and unknown flags
before converting a job to FPGA register writes. The render node may submit
validated jobs without DRM master privileges. Completion and dependencies use
the existing scheduler, dma-resv and timeline syncobj path.

The initial hardware has no programmable shader input. Gallium lowers clear and
solid triangle draws to fixed-function commands; unsupported state falls back
explicitly instead of being silently misrendered.

## Delivery stages

1. Lock rasterization semantics with the golden model and shared-edge tests.
2. Add the triangle RTL engine and compare every covered pixel against that
   model under memory stalls and error injection.
3. Add register/queue encoding, a validated DRM UAPI and scheduler fence tests.
4. Run a standalone render-node triangle program and direct-scanout its GEM.
5. Add a native TaruGX Gallium driver for clears and solid triangles.
6. Verify a Wayland EGL triangle uses TaruGX hardware counters with llvmpipe
   absent, then benchmark CPU time and frame rate.
7. Extend the pipeline with interpolated color, depth, texture sampling and a
   deliberately bounded shader ISA.

The first milestone is intentionally fixed-function. A programmable shader ISA
before the memory, validation and fence contracts are proven would multiply the
debugging surface and make GPU-reset safety harder to establish.
