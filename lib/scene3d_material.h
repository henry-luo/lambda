#pragma once

// shared map vocabulary keeps import, export and the native binding plan in agreement.
enum Scene3dMapSlot {
    SCENE_MAP_BASE, SCENE_MAP_METAL_ROUGH, SCENE_MAP_NORMAL, SCENE_MAP_OCCLUSION, SCENE_MAP_EMISSIVE,
    SCENE_MAP_CLEARCOAT, SCENE_MAP_CLEARCOAT_ROUGHNESS, SCENE_MAP_CLEARCOAT_NORMAL,
    SCENE_MAP_SHEEN_COLOR, SCENE_MAP_SHEEN_ROUGHNESS, SCENE_MAP_TRANSMISSION, SCENE_MAP_THICKNESS, SCENE_MAP_COUNT
};
enum Scene3dMapOwner { SCENE_MAP_PBR, SCENE_MAP_ROOT, SCENE_MAP_COAT, SCENE_MAP_SHEEN, SCENE_MAP_TRANSMIT, SCENE_MAP_VOLUME, SCENE_MAP_IOR };
struct Scene3dExtensionSpec { Scene3dMapOwner owner; const char* name; };
static const Scene3dExtensionSpec scene3d_extension_specs[]={
    {SCENE_MAP_COAT,"KHR_materials_clearcoat"},{SCENE_MAP_SHEEN,"KHR_materials_sheen"},
    {SCENE_MAP_TRANSMIT,"KHR_materials_transmission"},{SCENE_MAP_VOLUME,"KHR_materials_volume"},{SCENE_MAP_IOR,"KHR_materials_ior"}
};
static constexpr unsigned SCENE_EXTENSION_COUNT=sizeof(scene3d_extension_specs)/sizeof(*scene3d_extension_specs);
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
    {"sheen-roughness-texture","sheenRoughnessTexture","sheen_roughness_map",SCENE_MAP_SHEEN,false},
    {"transmission-texture","transmissionTexture","transmission_map",SCENE_MAP_TRANSMIT,false},
    {"thickness-texture","thicknessTexture","thickness_map",SCENE_MAP_VOLUME,false}
};

enum Scene3dPhysicalValue {
    SCENE_COAT_FACTOR, SCENE_COAT_ROUGHNESS, SCENE_COAT_NORMAL_SCALE,
    SCENE_SHEEN_FACTOR, SCENE_SHEEN_ROUGHNESS, SCENE_TRANSMISSION, SCENE_IOR, SCENE_THICKNESS,
    SCENE_ATTENUATION_DISTANCE, SCENE_PHYSICAL_COUNT
};
enum Scene3dPhysicalRange { SCENE_UNIT, SCENE_FINITE, SCENE_NONNEGATIVE, SCENE_REFRACTION, SCENE_DISTANCE };
struct Scene3dPhysicalSpec {
    const char* attribute; float fallback; Scene3dPhysicalRange range;
    Scene3dMapOwner owner; const char* gltf; float gltf_fallback;
};
static const Scene3dPhysicalSpec scene3d_physical_specs[SCENE_PHYSICAL_COUNT]={
    {"clearcoat",0,SCENE_UNIT,SCENE_MAP_COAT,"clearcoatFactor",0},
    {"clearcoat-roughness",0,SCENE_UNIT,SCENE_MAP_COAT,"clearcoatRoughnessFactor",0},
    {"clearcoat-normal-scale",1,SCENE_FINITE,SCENE_MAP_COAT,nullptr,1},
    {"sheen",0,SCENE_UNIT,SCENE_MAP_SHEEN,nullptr,0},
    {"sheen-roughness",1,SCENE_UNIT,SCENE_MAP_SHEEN,"sheenRoughnessFactor",0},
    {"transmission",0,SCENE_UNIT,SCENE_MAP_TRANSMIT,"transmissionFactor",0},
    {"ior",1.5f,SCENE_REFRACTION,SCENE_MAP_IOR,"ior",1.5f},
    {"thickness",0,SCENE_NONNEGATIVE,SCENE_MAP_VOLUME,"thicknessFactor",0},
    // omitted distance means no absorption; zero is internal and is never an authored distance.
    {"attenuation-distance",0,SCENE_DISTANCE,SCENE_MAP_VOLUME,"attenuationDistance",0}
};
static inline bool scene3d_physical_valid(const Scene3dPhysicalSpec& spec,double value,bool authored) {
    switch(spec.range) {
    case SCENE_UNIT:return value>=0&&value<=1;
    case SCENE_NONNEGATIVE:return value>=0;
    case SCENE_REFRACTION:return value>=1;
    case SCENE_DISTANCE:return value>0||(!authored&&value==0);
    default:return true; // callers reject nonfinite numbers before this range check.
    }
}
enum Scene3dPhysicalColor { SCENE_SHEEN_COLOR, SCENE_ATTENUATION_COLOR, SCENE_PHYSICAL_COLORS };
struct Scene3dPhysicalColorSpec {
    const char *attribute,*sampler,*fallback; Scene3dMapOwner owner; const char* gltf;
    float gltf_fallback; int factor;
};
static const Scene3dPhysicalColorSpec scene3d_physical_colors[SCENE_PHYSICAL_COLORS]={
    {"sheen-color","sheen_color","#ffffff",SCENE_MAP_SHEEN,"sheenColorFactor",0,SCENE_SHEEN_FACTOR},
    {"attenuation-color","attenuation_color","#ffffff",SCENE_MAP_VOLUME,"attenuationColor",1,-1}
};
