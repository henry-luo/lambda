#pragma once
#include "mark_reader.hpp"
#include "../../lib/navigation.h"
#include "../../lib/arraylist.hpp"
#include "../../lib/hashmap_typed.hpp"
struct NavigationBinding {char name[128];uint64_t id;};
struct NavigationLinkBinding {NavigationBinding binding;char kind[32];NavigationLinkDesc description;};
enum NavigationKind {NAVIGATION_GRAPH,NAVIGATION_GRID,NAVIGATION_MESH,NAVIGATION_BAKE,NAVIGATION_INVALID};
NavigationKind navigation_kind(const char* text);
const char* navigation_kind_tag(NavigationKind kind);
struct NavigationCompiled {
    NavigationGraph* graph=nullptr;
    NavigationMesh* mesh=nullptr;
    NavigationBake* bake=nullptr;
    NavigationLinks* links=nullptr;
    lam::ArrayList<NavigationLinkBinding> link_bindings;
    size_t max_attachment_tests=NAVIGATION_MAX_ATTACHMENT_TESTS;
    char source_hash[65]={},settings_hash[65]={};
    unsigned dimensions=2;
    size_t size[3]={1,1,1};
    NavigationKind kind=NAVIGATION_GRAPH;
    lam::ArrayList<NavigationBinding> bindings;
    TypedHashMap<NavigationBinding,HashMapCStrMemberKeyOps<NavigationBinding,&NavigationBinding::name>> ids={};
    char diagnostic[256]={};
    ~NavigationCompiled(){navigation_links_destroy(links);if(mesh)navigation_mesh_destroy(mesh);else if(bake)navigation_bake_destroy(bake);else navigation_destroy(graph);ids.destroy();}
};
bool navigation_compile(NavigationCompiled* compiled,ItemReader source);
bool navigation_compiled_equal(const NavigationCompiled& left,const NavigationCompiled& right);
uint64_t navigation_reference(const NavigationCompiled& compiled,ItemReader value);
bool navigation_number(ItemReader value,double fallback,double maximum,double* number);
bool navigation_integer(ItemReader value,double fallback,size_t maximum,size_t* result);
bool navigation_vector(ItemReader value,unsigned dimensions,NavigationVec fallback,NavigationVec* result);
bool navigation_boolean(ItemReader value,bool fallback,bool* result);
const char* navigation_text(ItemReader value);
ItemReader navigation_field(ItemReader source,const char* key);
bool navigation_bake_settings_read(ItemReader source,NavigationBakeSettings* settings);
bool navigation_links_read(NavigationCompiled* compiled,ItemReader source);
bool navigation_link_declarations_equal(const NavigationCompiled& left,const NavigationCompiled& right);
bool navigation_build_link_overlay(const NavigationCompiled& compiled,const NavigationSurfaceView& view,NavigationLinks** links,NavigationError* error);
bool navigation_attach_links(NavigationCompiled* compiled);
// shared by pure result conversion and native scene agents; no runtime allocation or script calls.
bool navigation_search_read(const NavigationCompiled& compiled,ItemReader options,NavigationQuery* query,NavigationError* error);
bool navigation_surface_read(const NavigationCompiled& compiled,ItemReader options,NavigationSurfaceQuery* query,NavigationError* error);
bool navigation_surface_find_compiled(const NavigationCompiled& compiled,const NavigationSurfaceQuery* query,NavigationMeshPath* path,NavigationError* error);
