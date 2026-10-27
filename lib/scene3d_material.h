#pragma once

// shared map vocabulary keeps import, export and the native binding plan in agreement.
enum Scene3dMapSlot {
    SCENE_MAP_BASE, SCENE_MAP_METAL_ROUGH, SCENE_MAP_NORMAL, SCENE_MAP_OCCLUSION, SCENE_MAP_EMISSIVE,
    SCENE_MAP_CLEARCOAT, SCENE_MAP_CLEARCOAT_ROUGHNESS, SCENE_MAP_CLEARCOAT_NORMAL,
    SCENE_MAP_SHEEN_COLOR, SCENE_MAP_SHEEN_ROUGHNESS, SCENE_MAP_COUNT
};
enum Scene3dMapOwner { SCENE_MAP_PBR, SCENE_MAP_ROOT, SCENE_MAP_COAT, SCENE_MAP_SHEEN };
struct Scene3dMapSpec {
    const char *attribute, *gltf, *sampler;
    Scene3dMapOwner owner;
    bool srgb;
};
static const Scene3dMapSpec scene3d_map_specs[SCENE_MAP_COUNT]={
    {"texture","baseColorTexture","image",SCENE_MAP_PBR,true},
    {"metal-rough-texture","metallicRoughnessTexture","metal_rough",SCENE_MAP_PBR,false},
    {"normal-texture","normalTexture","normal_map",SCENE_MAP_ROOT,false},
    {"occlusion-texture","occlusionTexture","occlusion",SCENE_MAP_ROOT,false},
    {"emissive-texture","emissiveTexture","emissive_map",SCENE_MAP_ROOT,true},
    {"clearcoat-texture","clearcoatTexture","coat_map",SCENE_MAP_COAT,false},
    {"clearcoat-roughness-texture","clearcoatRoughnessTexture","coat_roughness_map",SCENE_MAP_COAT,false},
    {"clearcoat-normal-texture","clearcoatNormalTexture","coat_normal_map",SCENE_MAP_COAT,false},
    {"sheen-color-texture","sheenColorTexture","sheen_color_map",SCENE_MAP_SHEEN,true},
    {"sheen-roughness-texture","sheenRoughnessTexture","sheen_roughness_map",SCENE_MAP_SHEEN,false}
};

enum Scene3dPhysicalValue {
    SCENE_COAT_FACTOR, SCENE_COAT_ROUGHNESS, SCENE_COAT_NORMAL_SCALE,
    SCENE_SHEEN_FACTOR, SCENE_SHEEN_ROUGHNESS, SCENE_PHYSICAL_COUNT
};
struct Scene3dPhysicalSpec { const char* attribute; float fallback; bool unit_interval; };
static const Scene3dPhysicalSpec scene3d_physical_specs[SCENE_PHYSICAL_COUNT]={
    {"clearcoat",0,true},{"clearcoat-roughness",0,true},{"clearcoat-normal-scale",1,false},
    {"sheen",0,true},{"sheen-roughness",1,true}
};
