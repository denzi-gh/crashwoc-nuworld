# Native projected character shadows

The game no longer reads `.shd` files. `ShadDataLoad` and `InstShadDataLoad` return a shared compatibility handle;
animation lookup returns a shared entry. These keep existing game visibility rules and call sites working without
file probes, loading-heap allocations, shadow textures or CLUTs. The original files can remain on the imported disc.

The caster is the actual visible model, including rigid joint meshes, GPU skinning and blend-shape weights. The
renderer borrows its first material passes and its existing mesh buffers and joint snapshots. It does not evaluate
the animation a second time. A character's reflected model is excluded. Source records reset with the frame arena;
the shared sorting material resets when game materials are released.

The receiver is the ground plane supplied by the game, including its slope. The old flattened transform's missing
normal is recovered from its X and Z axes. The strongest available directional light determines projection;
missing, below-ground or grazing directions fall back to projection along the receiver normal. This bounds shadow
stretching. Original atlas size and animation frame no longer determine the shadow shape.

Each caster uses two geometry passes:

1. Mark depth-qualified receiver pixels in the existing stencil buffer using a plane 0.075 world units below the
   supplied plane. No colour or depth is written.
2. Draw the projected silhouette on the supplied plane, with at most 25% opacity. Each accepted stencil sample is
   consumed once, so overlapping limbs and triangles do not repeatedly darken it. Depth and framebuffer alpha stay
   unchanged. Material texture alpha is retained for cutouts.

The narrow depth band rejects absent ground, more distant floors and foreground occluders. It is a local planar
receiver, not a general solution for stairs, highly uneven terrain, walls, self-shadowing or shadowed point lights.
Those need receiver geometry or a light-space shadow-map path. Edges use the existing MSAA; there is no blur pass.
The existing game distance and visibility rules still apply, with a hard renderer budget of 32 casters per frame.

## Cost and inspection

There are no additional shadow textures, framebuffer objects, mesh buffers or bone uploads. There are additional
CPU draw records and two submissions of each caster mesh, with GPU skinning/morphing performed in both passes.
For example, a 5,000-triangle caster adds 10,000 submitted triangles, plus its projected pixel coverage. This is not
a whole-frame performance percentage: resolution, MSAA, character count, mesh complexity and the GPU matter.

In the F1 **Render** menu, **Native shadows** shows actual caster, draw and submitted-triangle counts. **Disable
native shadows** supports visual and performance comparisons; it is a debug toggle, not a saved setting. CPU caster
capture still runs when this render-thread toggle is off.

## Validation

`ctest --test-dir build/release -R "^(shadcom|shadow-gl|interp)$" --output-on-failure`

`shadcom` covers operation without a VFS/SHD files, unchanged loading buffers, receiver projection, collapsed
receiver transforms, invalid/grazing lights, reflection exclusion and shared joint palettes. `shadow-gl` exercises
the production backend in a hidden OpenGL window, checking uniform opacity over overlapping geometry, ground
holes, lower floors, moving bones, disabling shadows and MSAA. It skips if OpenGL 3.3 is unavailable.
