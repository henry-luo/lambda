# Lambda 3D versus Three.js — review

**Date:** 2026-10-28. **Status:** informative review of the current checkout and recorded validation; no new benchmark was run for this review.

**Scope:** native `lambda.scene3d` capabilities compared with Three.js, distinguished from running Three.js inside LambdaJS. This review introduces no design rulings. Pure scene construction follows **S12.1.1v3** in the [formal semantics](../../doc/Lambda_Formal_Semantics.md).

**Lambda now has a capable core 3D renderer, but Three.js remains substantially broader and more extensible.** Current native shader, postprocessing and outdoor profiles are recorded below; complete Three.js compatibility is not claimed.

There are two comparisons to distinguish:

- **Native Lambda 3D:** `lambda.scene3d` describes scenes as Lambda elements; Radiant renders them.
- **Three.js inside Lambda:** LambdaJS already runs unmodified Three.js **0.186.1**, including selected controls and postprocessing addons. Compatibility is validated for selected workloads, rather than the entire Three.js feature set. See the [demo documentation](../../test/demo/scene3d/README.md) and [WebGL Phase III evidence](../impl/Radiant_WebGL_Phase3.md).

For the **native renderer**, the current checkout compares as follows:

| Area | Lambda native 3D | Three.js |
|---|---|---|
| Scene basics | Hierarchies, transforms, cameras, indexed meshes, shared resources, instancing | Broader scene, geometry and object APIs |
| Materials | Basic, Lambert, metallic–roughness PBR; mapped physical clearcoat/sheen/transmission; thin foliage, flow normals, transparency and alpha masks | Broader physical materials, including anisotropy, iridescence and dispersion. [MeshPhysicalMaterial](https://threejs.org/docs/pages/MeshPhysicalMaterial.html) |
| Lighting | Ambient, hemisphere, directional, point and spot; procedural/HDR environments; directional cascades, spot and point shadows; distance/height fog | Broader lighting configuration and rendering options |
| Animation | Clips, blending, crossfades, skeletal animation and morphs; currently 256 bones per skeleton, four influences per vertex, two morph targets | More general animation and deformation support |
| Assets | Selected OBJ/MTL, glTF/GLB, STL, PLY, A3D and FBX/BVH profiles | Broader loader coverage and glTF extensions, including Draco and compressed textures. [GLTFLoader](https://threejs.org/docs/pages/GLTFLoader.html) |
| Custom graphics | GLSL shader materials, named instance attributes, fixed-step GPU simulations, HDR bloom/FXAA/custom color passes | GLSL shaders, multipass effects, and node/TSL materials. [ShaderMaterial](https://threejs.org/docs/pages/ShaderMaterial.html), [EffectComposer](https://threejs.org/docs/pages/EffectComposer.html) |
| Backend | Native OpenGL; documented qualification centers on macOS | WebGL2 plus a WebGPU renderer with WebGL2 fallback. [WebGPURenderer](https://threejs.org/manual/pages/webgpurenderer.html) |

Native feature evidence is recorded in the [PBR and shadow implementation record](../impl/Radiant_Scene3d_PBR_Shadows.md), [asset-loading design](Radiant_Design_WebGL.md#15-asset-loading-into-native-scenes), and [motion implementation record](../impl/Lambda_Impl_Motion.md). The current [renderer](../../radiant/scene3d.cpp) defines the 256-bone limit (`SCENE_BONE_LIMIT`), four skin influences and two morph targets (`Scene3dVertex`); older Phase III descriptions of a 16-bone limit predate the palette upgrade. Shader/GPU simulation and M0–M7 are implemented for bounded native profiles; see the [current feature record](../impl/Radiant_Impl_Scene3d_Three_Features.md) and [outdoor qualification](../impl/Radiant_Impl_Scene3d_Eden.md).

The recent native PBR work is significant: **materials, environment lighting and shadows closely match Three.js in the selected reference scenes**—approximately 99.79–99.94% of RGB channel samples differ by at most 8/255. That measures those fixtures, rather than overall feature parity. The comparisons use the pinned Three.js r186 oracle and cover direct lighting, maps, procedural and HDR environments, and directional, spot and point shadows. See the [milestone evidence](../impl/Radiant_Scene3d_PBR_Shadows.md#11-progress).

Lambda's particular advantage is **composing 3D with its data processing, scientific charts, HTML/CSS and SVG**. Scene construction remains pure data computation, with playback and rendering handled by the host, following **S12.1.1v3**. The [scientific chart package](../../doc/Lambda_Chart_Plot.md) combines native 3D scenes with SVG guides, camera interaction and source picking. Three.js offers broader rendering pipelines and asset compatibility. Native Lambda now supports substantial custom graphics, while arbitrary shader shadows, depth effects and richer water remain bounded follow-on work.

Performance needs separate qualification. The last documented rich **Three.js-on-Lambda** workload measured a median of **307.676 ms per frame, roughly 3 FPS**, on an Apple M4. The measurement covers an 800 × 500 PBR frame with postprocessing, snapshot readback and page composition. It is neither a native `scene3d` benchmark nor a browser speed comparison, but it prevents assuming speed parity from feature support. See the [release measurements](../impl/Radiant_WebGL_Phase3.md#release-measurements).

**Assessment:** Lambda is useful today for integrated visualizations, model viewers and bounded interactive scenes. Advanced depth/temporal effects, arbitrary shader/depth contracts and wider asset/backend compatibility remain meaningful gaps. Garden fidelity also depends on geometry, textures, planting density and authored materials.
