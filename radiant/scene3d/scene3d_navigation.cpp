#include "scene3d.hpp"
#include "scene3d_source.hpp"
#include "../event.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../../lambda/input/css/dom_lifecycle.hpp"
#include "../../lambda/input/input.hpp"
#include "../../lambda/input/input-parsers.h"
#include "../../lambda/runtime/lambda-navigation.hpp"
#include "../../lambda/runtime/value_builder.hpp"
#include "../../lambda/runtime/lambda-error.h"
#include "../../lib/mem.h"
#include "../../lib/thread_pool.h"
#include <math.h>
#include <float.h>

struct Scene3dNavigationSource {char id[128]={};bool walkable=true;uint32_t groups=UINT32_MAX;double cost=1;};
// this is the entire worker boundary: no DOM refs, Item values or script callbacks.
struct Scene3dBakeWorker {
    Scene3dNavigationGeometry geometry;NavigationBakeSettings settings={};NavigationBake* result=nullptr;NavigationBakeStats stats={};
    bool cancelled=false,started=false,done=false,success=false;unsigned stage=0;size_t work=0;char diagnostic[256]={};
    ~Scene3dBakeWorker(){navigation_bake_destroy(result);}
};
struct Scene3dBakeJob {
    Scene3dBakeJob* next=nullptr;Scene3dBakeWorker worker;DomNodeRef root={};lam::ArrayList<DomNodeRef> references;
    NavigationCompiled definitions;
    char asset[128]={},settings_hash[65]={},diagnostic[256]={};uint64_t token=0,target_generation=0,published_generation=0;bool stale=false;
};

struct Scene3dNavigationAsset {
    Scene3dNavigationAsset* next=nullptr;
    DomNodeRef source={};
    char id[128]={};
    uint64_t key=0,generation=0;
    uint32_t revision=0;
    NavigationCompiled compiled;
    bool source_bake=false;uint64_t geometry_key=0,latest_job=0;
    NavigationBakeSettings settings={};lam::ArrayList<Scene3dNavigationSource> sources;
};
struct Scene3dNavigation {
    DomDocument* document=nullptr;DomNodeRef root={};Scene3dRuntime* runtime=nullptr;
    Scene3dNavigationAsset* assets=nullptr;
    Scene3dNavigationStats stats={};
    uint64_t epoch=UINT64_MAX,rejected_epoch=UINT64_MAX;
    uint64_t* next_generation=nullptr;
    char diagnostic[256]={};
    ThreadPool* pool=nullptr;Scene3dBakeJob* jobs=nullptr;
};
static void nav_job_destroy(Scene3dBakeJob* job){if(job){job->~Scene3dBakeJob();mem_free(job);}}
static void nav_job_stale(Scene3dBakeJob* job,const char* message) {
    if(job->published_generation)return;job->stale=true;str_copy(job->diagnostic,sizeof(job->diagnostic),message,strlen(message));
    __atomic_store_n(&job->worker.cancelled,true,__ATOMIC_RELAXED);
}
static size_t nav_source_views(const Scene3dNavigationAsset* asset,Scene3dNavigationSelection* sources) {
    size_t i=0;for(const auto& source:asset->sources)sources[i++]={source.id,source.walkable,source.groups,source.cost};return i;
}
static bool nav_source_key(const Scene3dNavigationAsset* asset,DomElement* root,uint64_t* key,char* diagnostic,size_t capacity) {
    Scene3dNavigationSelection sources[64];size_t count=nav_source_views(asset,sources);
    return scene3d_navigation_geometry_key(root,sources,count,key,diagnostic,capacity);
}
static bool nav_source_snapshot(const Scene3dNavigationAsset* asset,DomElement* root,Scene3dNavigationGeometry* geometry,lam::ArrayList<DomNodeRef>* refs,char* diagnostic,size_t capacity) {
    Scene3dNavigationSelection sources[64];size_t count=nav_source_views(asset,sources);
    return scene3d_collect_navigation_geometry(root,sources,count,geometry,refs,diagnostic,capacity);
}
static bool nav_asset_ready(const Scene3dNavigationAsset* asset){return !asset->source_bake||asset->compiled.bake;}
static size_t nav_storage_nodes(const NavigationCompiled& compiled) {
    if(!compiled.bake)return navigation_node_count(compiled.graph);
    // merged polygons still retain their backing cells; graph compression cannot expand the cache budget.
    NavigationBakeStats stats={};navigation_bake_stats(compiled.bake,&stats);return stats.cells+2*navigation_links_count(compiled.links);
}
static bool nav_counts_fit(size_t nodes,size_t edges,size_t vertices,size_t storage_nodes) {
    return nodes<=NAVIGATION_MAX_NODES&&edges<=NAVIGATION_MAX_EDGES&&vertices<=NAVIGATION_MAX_VERTICES&&storage_nodes<=NAVIGATION_MAX_NODES;
}
bool scene3d_navigation_tag(DomElement* node) {
    const char* tag=node->local_name();return !strncmp(tag,"navigation-",11)&&navigation_kind(tag)!=NAVIGATION_INVALID;
}
static void nav_asset_destroy(Scene3dNavigationAsset* asset){if(asset){asset->~Scene3dNavigationAsset();mem_free(asset);}}
Scene3dNavigation* scene3d_navigation_create(DomElement* root,Scene3dRuntime* runtime) {
    uint64_t* sequence=scene3d_navigation_sequence(root->doc);if(!sequence)return nullptr;
    void* memory=mem_alloc(sizeof(Scene3dNavigation),MEM_CAT_RENDER);if(!memory)return nullptr;
    auto* state=new(memory) Scene3dNavigation();state->document=root->doc;state->root=dom_node_ref(root);state->runtime=runtime;state->next_generation=sequence;return state;
}
void scene3d_navigation_destroy(Scene3dNavigation* state) {
    if(!state)return;
    for(auto* job=state->jobs;job;job=job->next)__atomic_store_n(&job->worker.cancelled,true,__ATOMIC_RELAXED);
    // joining precedes any native input/result teardown; workers never borrow the document.
    tp_destroy(state->pool);
    while(state->jobs){auto* job=state->jobs;state->jobs=job->next;nav_job_destroy(job);}
    while(state->assets){auto* asset=state->assets;state->assets=asset->next;nav_asset_destroy(asset);}state->~Scene3dNavigation();mem_free(state);
}
static bool nav_asset_placement(DomElement* root,DomElement* node,unsigned depth,unsigned* work) {
    if(depth>64||++*work>8192)return false;
    if(scene3d_navigation_tag(node)&&(!node->parent||!node->parent->is_element()||
        strcmp(node->parent->as_element()->local_name(),"resources")||node->parent->parent!=root))return false;
    for(DomNode* child=node->first_child;child;child=child->next_sibling)
        if(child->is_element()&&!nav_asset_placement(root,child->as_element(),depth+1,work))return false;
    return true;
}
static bool nav_source_compile(Scene3dNavigationAsset* asset,DomElement* root,ItemReader source,char* diagnostic,size_t capacity) {
    auto fail=[&](const char* message){str_copy(diagnostic,capacity,message,strlen(message));return false;};
    auto list=navigation_field(source,"sources");NavigationError error={};double version;
    const char* kind=source.isElement()?source.asElement().tagName():navigation_text(source.asMap().get("type"));
    if(navigation_kind(kind)!=NAVIGATION_BAKE||!navigation_number(navigation_field(source,"version"),1,1,&version)||version!=1||
        !navigation_field(source,"triangles").isNull()||!navigation_field(source,"format").isNull()||!list.isArray()||!list.asArray().length()||list.asArray().length()>64||
        !navigation_bake_settings_read(navigation_field(source,"settings"),&asset->settings))return fail("navigation: source bake requires schema 1, settings and 1..64 selections");
    if(!navigation_bake_settings_valid(&asset->settings,nullptr,nullptr,&error))return fail(error.message);
    for(int64_t i=0;i<list.asArray().length();i++) {
        auto value=list.asArray().get(i);const char* id=navigation_text(value.isMap()?value.asMap().get("id"):value);Scene3dNavigationSource entry;size_t groups;
        if(!id||!*id||strlen(id)>=sizeof(entry.id)||!navigation_boolean(value.asMap().get("walkable"),true,&entry.walkable)||
            !navigation_integer(value.asMap().get("groups"),UINT32_MAX,UINT32_MAX,&groups)||!navigation_number(value.asMap().get("cost"),1,DBL_MAX,&entry.cost))return fail("navigation: invalid source selection");
        for(const auto& before:asset->sources)if(!strcmp(before.id,id))return fail("navigation: duplicate source selection");
        strcpy(entry.id,id);entry.groups=(uint32_t)groups;if(!asset->sources.append(entry))return fail("navigation: selection allocation failed");
    }
    if(!nav_source_key(asset,root,&asset->geometry_key,diagnostic,capacity))return false;
    asset->source_bake=true;asset->compiled.kind=NAVIGATION_BAKE;asset->compiled.dimensions=3;
    if(!navigation_links_read(&asset->compiled,source))return fail(asset->compiled.diagnostic);
    return navigation_bake_settings_hash(&asset->settings,asset->compiled.settings_hash)||fail("navigation: settings fingerprint failed");
}
static bool nav_source_definition_equal(const Scene3dNavigationAsset* a,const Scene3dNavigationAsset* b) {
    if(!a->source_bake||!b->source_bake)return false;
    if(strcmp(a->compiled.settings_hash,b->compiled.settings_hash)||a->sources.size()!=b->sources.size())return false;
    for(size_t i=0;i<a->sources.size();i++){const auto &x=a->sources[i],&y=b->sources[i];
        if(strcmp(x.id,y.id)||x.walkable!=y.walkable||x.groups!=y.groups||x.cost!=y.cost)return false;}
    return true;
}
static bool nav_asset_equal(const Scene3dNavigationAsset* a,const Scene3dNavigationAsset* b) {
    if(a->source_bake!=b->source_bake)return false;if(!a->source_bake)return navigation_compiled_equal(a->compiled,b->compiled);
    return nav_source_definition_equal(a,b)&&navigation_link_declarations_equal(a->compiled,b->compiled);
}
static bool nav_asset_compile(Scene3dNavigationAsset* asset,DomElement* root,DomElement* node,char* diagnostic,size_t capacity) {
    auto fail=[&](const char* message){str_copy(diagnostic,capacity,message,strlen(message));return false;};
    const char* transforms[]={"position","rotation","quaternion","scale","matrix"};
    for(const char* key:transforms)if(node->has_attribute(key)||!scene3d_value(node,key).isNull())return fail("navigation: assets use world coordinates and cannot have object transforms");
    for(DomNode* child=node->first_child;child;child=child->next_sibling)
        if(child->is_element()||(child->is_text()&&child->as_text()->native_string&&strspn(child->as_text()->native_string->chars," \r\n\t")!=child->as_text()->native_string->len))return fail("navigation: assets have no child content");
    double version;if(!scene3d_real(node,"version",1,&version)||version!=1)return fail("navigation: asset requires schema version 1");
    struct Scratch {Pool* pool=nullptr;~Scratch(){if(pool)pool_destroy(pool);}} scratch;
    auto data=scene3d_value(node,"data");ItemReader source;
    const char* json=data.isString()?data.asString()->chars:data.isNull()?scene3d_text(node,"data"):nullptr;
    if(!data.isNull()||json) {
        if(json) {
            if(strlen(json)>16*1024*1024||(data.isString()&&strlen(json)!=data.asString()->len))return fail("navigation: asset JSON byte quota exceeded or embedded NUL");
            scratch.pool=pool_create();if(!scratch.pool)return fail("navigation: JSON scratch allocation failed");
            Input* input=Input::create(scratch.pool);if(!input)return fail("navigation: JSON input allocation failed");
            bool valid=false;Item parsed=parse_json_to_item_strict(input,json,&valid);
            if(!valid||get_type_id(parsed)!=LMD_TYPE_MAP)return fail("navigation: data requires a JSON object");source=ItemReader(parsed.to_const());
        }else if(data.isMap())source=data;
        else return fail("navigation: data requires a map or JSON object string");
    }else {
        Element* backing=dom_element_backing(node);if(!backing)return fail("navigation: asset has no authored data");source=ItemReader(Item{.element=backing}.to_const());
    }
    if(!navigation_field(source,"sources").isNull()) {
        if(strcmp(node->local_name(),"navigation-bake"))return fail("navigation: only bake assets accept source selections");
        return nav_source_compile(asset,root,source,diagnostic,capacity);
    }
    if(!navigation_compile(&asset->compiled,source))return fail(asset->compiled.diagnostic);
    if(strcmp(node->local_name(),navigation_kind_tag(asset->compiled.kind)))return fail("navigation: asset tag and data kind disagree");
    return true;
}
bool scene3d_navigation_prepare(Scene3dNavigation* state,DomElement* root,char* diagnostic,size_t capacity) {
    if(!state||state->document!=root->doc)return false;
    auto report=[&](){str_copy(diagnostic,capacity,state->diagnostic,strlen(state->diagnostic));return false;};
    if(state->epoch==root->doc->mutation_epoch)return true;
    if(state->rejected_epoch==root->doc->mutation_epoch)return report();
    auto reject=[&](const char* message){str_copy(state->diagnostic,sizeof(state->diagnostic),message,strlen(message));state->rejected_epoch=root->doc->mutation_epoch;return report();};
    Scene3dRuntimeStats clock={};scene3d_runtime_stats(state->runtime,&clock);
    if(!scene3d_runtime_can_configure(state->runtime,clock.timestep,1)) {
        // sampling is transient; retry this same authored epoch after the tick finishes.
        const char* message="navigation: authored edits are unavailable during tick sampling";
        str_copy(diagnostic,capacity,message,strlen(message));return false;
    }
    unsigned work=0;if(!nav_asset_placement(root,root,0,&work))return reject("navigation: assets belong directly to scene resources; scene depth/node quota exceeded");
    struct Plan {Scene3dNavigationAsset* chosen;Scene3dNavigationAsset* created;DomNodeRef source;uint64_t key;uint32_t revision;uint64_t geometry_key;};
    struct Plans {lam::ArrayList<Plan> values;~Plans(){for(const auto& plan:values)nav_asset_destroy(plan.created);}} plans;
    size_t nodes=0,edges=0,vertices=0,storage_nodes=0;uint64_t next_generation=*state->next_generation;
    for(DomNode* resource=root->first_child;resource;resource=resource->next_sibling) {
        if(!resource->is_element()||strcmp(resource->as_element()->local_name(),"resources"))continue;
        for(DomNode* child=resource->as_element()->first_child;child;child=child->next_sibling) {
            if(!child->is_element()||!scene3d_navigation_tag(child->as_element()))continue;auto* node=child->as_element();
            const char* id=scene3d_text(node,"id");if(!id||!*id||strlen(id)>=128||plans.values.size()>=64)return reject("navigation: assets require IDs shorter than 128 bytes and at most 64 definitions");
            for(const auto& plan:plans.values)if(!strcmp(plan.chosen->id,id))return reject("navigation: duplicate asset ID");
            uint64_t key=5381;unsigned work=0;if(!scene3d_source_key(node,&key,&work,0,false))return reject("navigation: source quota exceeded");
            double revision;if(!scene3d_real(node,"revision",0,&revision)||revision<0||revision>UINT32_MAX||floor(revision)!=revision)return reject("navigation: invalid asset revision");
            auto ref=dom_node_ref(node);Scene3dNavigationAsset* before=nullptr;
            for(auto* asset=state->assets;asset;asset=asset->next)if(!strcmp(asset->id,id)){before=asset;break;}
            bool same=before&&before->source.address==ref.address&&before->source.expected_id==ref.expected_id;
            Scene3dNavigationAsset *chosen=before,*created=nullptr;
            uint64_t geometry_key=before?before->geometry_key:0;
            bool source_changed=false;
            if(same&&before->source_bake&&before->key==key&&before->revision==(uint32_t)revision){uint64_t current;
                if(!nav_source_key(before,root,&current,state->diagnostic,sizeof(state->diagnostic))){state->rejected_epoch=root->doc->mutation_epoch;return report();}
                source_changed=current!=before->geometry_key;}
            if(!same||before->key!=key||before->revision!=(uint32_t)revision||source_changed) {
                void* memory=mem_alloc(sizeof(Scene3dNavigationAsset),MEM_CAT_RENDER);if(!memory)return reject("navigation: asset allocation failed");
                created=new(memory) Scene3dNavigationAsset();strcpy(created->id,id);state->stats.compilations++;
                if(!nav_asset_compile(created,root,node,state->diagnostic,sizeof(state->diagnostic))){nav_asset_destroy(created);state->rejected_epoch=root->doc->mutation_epoch;return report();}
                geometry_key=created->geometry_key;bool equivalent=same&&before->revision==(uint32_t)revision&&nav_asset_equal(before,created);
                bool same_source=same&&before->revision==(uint32_t)revision&&nav_source_definition_equal(before,created);
                if(same_source) {
                    bool geometry_same=before->geometry_key==geometry_key;
                    if(!geometry_same&&before->compiled.bake) {
                        Scene3dNavigationGeometry fresh;
                        if(!nav_source_snapshot(created,root,&fresh,nullptr,state->diagnostic,sizeof(state->diagnostic))){nav_asset_destroy(created);state->rejected_epoch=root->doc->mutation_epoch;return report();}
                        geometry_same=!strcmp(fresh.source_hash,before->compiled.source_hash);
                    }
                    equivalent=equivalent&&geometry_same;
                    if(!equivalent&&geometry_same&&before->compiled.bake) {
                        // link-only edits retain the published geometry and replace only connectivity.
                        created->compiled.bake=navigation_bake_retain(before->compiled.bake);
                        created->compiled.graph=const_cast<NavigationGraph*>(navigation_bake_graph(created->compiled.bake));strcpy(created->compiled.source_hash,before->compiled.source_hash);
                        if(!navigation_attach_links(&created->compiled)){str_copy(state->diagnostic,sizeof(state->diagnostic),created->compiled.diagnostic,strlen(created->compiled.diagnostic));nav_asset_destroy(created);state->rejected_epoch=root->doc->mutation_epoch;return report();}
                    }
                }
                if(equivalent){nav_asset_destroy(created);created=nullptr;}
                else {
                    if(next_generation>INT64_MAX){nav_asset_destroy(created);return reject("navigation: generation quota exceeded");}
                    created->generation=next_generation++;chosen=created;
                }
            }
            nodes+=navigation_node_count(chosen->compiled.graph);edges+=navigation_edge_count(chosen->compiled.graph);
            vertices+=navigation_mesh_vertex_count(chosen->compiled.mesh);
            storage_nodes+=nav_storage_nodes(chosen->compiled);
            if(!nav_counts_fit(nodes,edges,vertices,storage_nodes)){nav_asset_destroy(created);return reject("navigation: combined scene graph/vertex/storage quota exceeded");}
            if(!plans.values.append({chosen,created,ref,key,(uint32_t)revision,geometry_key})){nav_asset_destroy(created);return reject("navigation: reconciliation allocation failed");}
        }
    }
    // all candidates validate before any old owner is released; future jobs must invalidate at this boundary.
    for(auto* asset=state->assets;asset;) {
        auto* next=asset->next;bool kept=false;for(const auto& plan:plans.values)kept|=plan.chosen==asset;
        if(!kept){
            for(auto* job=state->jobs;job;job=job->next)if(!strcmp(job->asset,asset->id)&&job->target_generation==asset->generation)nav_job_stale(job,"navigation bake: target or source generation changed");
            state->stats.invalidations++;nav_asset_destroy(asset);
        }asset=next;
    }
    state->assets=nullptr;Scene3dNavigationAsset** tail=&state->assets;
    for(auto& plan:plans.values) {
        auto* asset=plan.chosen;asset->source=plan.source;asset->key=plan.key;asset->revision=plan.revision;asset->geometry_key=plan.geometry_key;asset->next=nullptr;
        *tail=asset;tail=&asset->next;plan.created=nullptr;
    }
    state->stats.assets=plans.values.size();state->stats.nodes=nodes;state->stats.edges=edges;state->stats.vertices=vertices;state->stats.storage_nodes=storage_nodes;*state->next_generation=next_generation;
    state->epoch=root->doc->mutation_epoch;state->rejected_epoch=UINT64_MAX;state->diagnostic[0]=0;return true;
}
void scene3d_navigation_follow_rebuild(Scene3dNavigation* state,DomNode* old_root,DomNode* new_root) {
    if(!state)return;view_state_ref_follow_rebuild(state->document,&state->root,old_root,new_root);
    for(auto* asset=state->assets;asset;asset=asset->next)view_state_ref_follow_rebuild(state->document,&asset->source,old_root,new_root);
    state->epoch=state->rejected_epoch=UINT64_MAX;
}
bool scene3d_navigation_stats(Scene3dNavigation* state,Scene3dNavigationStats* stats){if(!state||!stats)return false;*stats=state->stats;return true;}
bool scene3d_navigation_generation(Scene3dNavigation* state,const char* id,uint64_t* generation) {
    if(!state||!id||!generation)return false;
    for(auto* asset=state->assets;asset;asset=asset->next)if(!strcmp(asset->id,id)){*generation=asset->generation;return true;}return false;
}
static void nav_bake_worker(void* opaque) {
    auto* worker=(Scene3dBakeWorker*)opaque;__atomic_store_n(&worker->started,true,__ATOMIC_RELAXED);
    auto proceed=[](void* owner,NavigationBakeStage stage,size_t work){auto* state=(Scene3dBakeWorker*)owner;
        __atomic_store_n(&state->stage,(unsigned)stage,__ATOMIC_RELAXED);__atomic_store_n(&state->work,work,__ATOMIC_RELAXED);
        return !__atomic_load_n(&state->cancelled,__ATOMIC_RELAXED);};
    NavigationBakeDesc desc={worker->geometry.triangles.data(),worker->geometry.triangles.size(),worker->settings,proceed,worker};NavigationError error={};
    worker->success=navigation_bake_create(&desc,&worker->result,&error);
    if(worker->success)navigation_bake_stats(worker->result,&worker->stats);
    else if(error.message)str_copy(worker->diagnostic,sizeof(worker->diagnostic),error.message,strlen(error.message));
    // release/acquire publishes native result/error storage; the worker touches nothing afterward.
    __atomic_store_n(&worker->done,true,__ATOMIC_RELEASE);
}
static Item nav_job_status(Scene3dNavigation* state,Scene3dBakeJob* job) {
    bool done=__atomic_load_n(&job->worker.done,__ATOMIC_ACQUIRE),cancelled=__atomic_load_n(&job->worker.cancelled,__ATOMIC_RELAXED);
    const char* status=job->published_generation?"published":job->stale?"stale":cancelled?(done?"cancelled":"cancelling"):
        done?(job->worker.success?"ready":"failed"):__atomic_load_n(&job->worker.started,__ATOMIC_RELAXED)?"running":"queued";
    bool current=false;for(auto* asset=state->assets;asset;asset=asset->next)if(!strcmp(asset->id,job->asset))
        current=state->epoch==state->document->mutation_epoch&&job->published_generation&&asset->generation==job->published_generation;
    RootedValueBuilder value(RootedValueBuilder::DATA_MAP);value.put("token",Item{.item=i2it((int64_t)job->token)});value.put("asset",Item{.item=s2it(heap_create_name(job->asset))});
    value.put("status",Item{.item=s2it(heap_create_name(status))});value.put("finished",Item{.item=b2it(done)});value.put("current",Item{.item=b2it(current)});
    value.put("stage",Item{.item=i2it(__atomic_load_n(&job->worker.stage,__ATOMIC_RELAXED))});value.put("work",Item{.item=i2it((int64_t)__atomic_load_n(&job->worker.work,__ATOMIC_RELAXED))});
    value.put("generation",job->published_generation?Item{.item=i2it((int64_t)job->published_generation)}:ItemNull);
    value.put("source_hash",Item{.item=s2it(heap_create_name(job->worker.geometry.source_hash))});value.put("settings_hash",Item{.item=s2it(heap_create_name(job->settings_hash))});
    const char* diagnostic=*job->diagnostic?job->diagnostic:done&&*job->worker.diagnostic?job->worker.diagnostic:nullptr;
    value.put("diagnostic",diagnostic?Item{.item=s2it(heap_create_name(diagnostic))}:ItemNull);
    value.put("cells",done&&job->worker.success?Item{.item=i2it((int64_t)job->worker.stats.cells)}:ItemNull);return value.value.get();
}
static bool nav_can_change(Scene3dNavigation* state) {
    Scene3dRuntimeStats clock={};scene3d_runtime_stats(state->runtime,&clock);return scene3d_runtime_can_configure(state->runtime,clock.timestep,1);
}
static Item nav_bake_operate(Scene3dNavigation* state,DomElement* root,MapReader request,const char* operation,bool valid,const char* diagnostic) {
    auto fail=[](const char* message){return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,message,nullptr));};
    if(!strcmp(operation,"navigation_bake_start")) {
        if(!valid)return fail(diagnostic);if(!nav_can_change(state))return fail("navigation bake: unavailable during tick sampling");
        const char* id=navigation_text(request.get("asset"));Scene3dNavigationAsset* asset=nullptr;
        for(auto* candidate=state->assets;candidate;candidate=candidate->next)if(id&&!strcmp(candidate->id,id)){asset=candidate;break;}
        if(!asset||!asset->source_bake)return fail("navigation bake: requires a scene source-bake asset");
        size_t count=0,triangles=0;for(auto* before=state->jobs;before;before=before->next){count++;triangles+=before->worker.geometry.triangles.size();}
        if(count>=8)return fail("navigation bake: release completed jobs before exceeding eight retained jobs");
        if(*state->next_generation>INT64_MAX)return fail("navigation bake: token quota exceeded");
        void* memory=mem_alloc(sizeof(Scene3dBakeJob),MEM_CAT_RENDER);if(!memory)return fail("navigation bake: job allocation failed");
        auto* job=new(memory) Scene3dBakeJob();char error[256]={};
        if(!nav_source_snapshot(asset,root,&job->worker.geometry,&job->references,error,sizeof(error))){nav_job_destroy(job);return fail(error);}
        if(triangles+job->worker.geometry.triangles.size()>SCENE3D_BAKE_TRIANGLE_LIMIT){nav_job_destroy(job);return fail("navigation bake: combined retained snapshot triangle quota exceeded");}
        job->definitions.dimensions=asset->compiled.dimensions;job->definitions.max_attachment_tests=asset->compiled.max_attachment_tests;
        for(const auto& link:asset->compiled.link_bindings)if(!job->definitions.link_bindings.append(link)){nav_job_destroy(job);return fail("navigation bake: link snapshot allocation failed");}
        job->root=dom_node_ref(root);job->target_generation=asset->generation;job->worker.settings=asset->settings;
        strcpy(job->asset,asset->id);strcpy(job->settings_hash,asset->compiled.settings_hash);job->token=(*state->next_generation)++;
        if(!state->pool)state->pool=tp_create(1);
        if(!state->pool||!tp_submit(state->pool,nav_bake_worker,&job->worker)){nav_job_destroy(job);return fail("navigation bake: worker submission failed");}
        for(auto* before=state->jobs;before;before=before->next)if(!strcmp(before->asset,asset->id))nav_job_stale(before,"navigation bake: superseded by a newer job");
        asset->latest_job=job->token;job->next=state->jobs;state->jobs=job;return nav_job_status(state,job);
    }
    auto token=request.get("token");if(!token.isInt()||token.asInt()<=0)return fail("navigation bake: token must be an opaque positive integer");
    Scene3dBakeJob** link=&state->jobs;while(*link&&(*link)->token!=(uint64_t)token.asInt())link=&(*link)->next;
    auto* job=*link;if(!job)return fail("navigation bake: unknown or released token");
    if(!strcmp(operation,"navigation_bake_poll"))return nav_job_status(state,job);
    if(!nav_can_change(state))return fail("navigation bake: unavailable during tick sampling");
    bool done=__atomic_load_n(&job->worker.done,__ATOMIC_ACQUIRE);
    if(!strcmp(operation,"navigation_bake_cancel")) {
        if(!job->published_generation)__atomic_store_n(&job->worker.cancelled,true,__ATOMIC_RELAXED);return nav_job_status(state,job);
    }
    if(!strcmp(operation,"navigation_bake_release")) {
        if(!done)return fail("navigation bake: wait for worker completion before release");*link=job->next;nav_job_destroy(job);return Item{.item=b2it(true)};
    }
    if(!strcmp(operation,"navigation_bake_export")) {
        if(!done||!job->worker.success||job->stale||__atomic_load_n(&job->worker.cancelled,__ATOMIC_RELAXED))return fail("navigation bake: no exportable completed result");
        return navigation_bake_value(job->worker.result,job->worker.geometry.source_hash,job->settings_hash,&job->definitions);
    }
    if(strcmp(operation,"navigation_bake_publish"))return fail("navigation bake: unsupported job operation");
    if(job->published_generation||job->stale||!done||!job->worker.success||__atomic_load_n(&job->worker.cancelled,__ATOMIC_RELAXED))return nav_job_status(state,job);
    if(!valid)return fail(diagnostic);
    Scene3dNavigationAsset* asset=nullptr;for(auto* candidate=state->assets;candidate;candidate=candidate->next)if(!strcmp(candidate->id,job->asset)){asset=candidate;break;}
    if(dom_node_ref_validate(state->document,job->root)!=root||!asset||!asset->source_bake||asset->generation!=job->target_generation||
        asset->latest_job!=job->token||strcmp(asset->compiled.settings_hash,job->settings_hash)) {
        nav_job_stale(job,"navigation bake: root, target or settings generation changed");return nav_job_status(state,job);
    }
    Scene3dNavigationGeometry fresh;lam::ArrayList<DomNodeRef> refs;char error[256]={};
    if(!nav_source_snapshot(asset,root,&fresh,&refs,error,sizeof(error))) {
        nav_job_stale(job,*error?error:"navigation bake: source validation failed");return nav_job_status(state,job);
    }
    bool same=refs.size()==job->references.size()&&!strcmp(fresh.source_hash,job->worker.geometry.source_hash);
    for(size_t i=0;i<refs.size()&&same;i++)same=dom_node_ref_identity_compare(refs[i],job->references[i])==0;
    if(!same){nav_job_stale(job,"navigation bake: source snapshot or identity changed");return nav_job_status(state,job);}
    struct OverlayOwner {NavigationLinks* links=nullptr;~OverlayOwner(){navigation_links_destroy(links);}} overlay;
    NavigationSurfaceView surface={};navigation_bake_surface(job->worker.result,&surface);NavigationError link_error={};
    if(!navigation_build_link_overlay(job->definitions,surface,&overlay.links,&link_error))return fail(link_error.message);
    const NavigationGraph* graph=overlay.links?navigation_links_graph(overlay.links):navigation_bake_graph(job->worker.result);
    size_t old_nodes=navigation_node_count(asset->compiled.graph),old_edges=navigation_edge_count(asset->compiled.graph),
        nodes=state->stats.nodes-old_nodes+navigation_node_count(graph),edges=state->stats.edges-old_edges+navigation_edge_count(graph);
    size_t storage_nodes=state->stats.storage_nodes-nav_storage_nodes(asset->compiled)+job->worker.stats.cells+2*navigation_links_count(overlay.links);
    if(!nav_counts_fit(nodes,edges,state->stats.vertices,storage_nodes))return fail("navigation bake: combined scene graph/storage quota exceeded");
    if(*state->next_generation>INT64_MAX)return fail("navigation bake: generation quota exceeded");
    // publication changes one native owner after all provenance/quota checks, never authored DOM.
    navigation_links_destroy(asset->compiled.links);navigation_bake_destroy(asset->compiled.bake);asset->compiled.bake=navigation_bake_retain(job->worker.result);
    asset->compiled.links=overlay.links;overlay.links=nullptr;asset->compiled.graph=const_cast<NavigationGraph*>(graph);strcpy(asset->compiled.source_hash,job->worker.geometry.source_hash);
    asset->generation=(*state->next_generation)++;job->published_generation=asset->generation;state->stats.nodes=nodes;state->stats.edges=edges;
    state->stats.storage_nodes=storage_nodes;
    state->stats.compilations++;state->stats.invalidations+=old_nodes!=0;return nav_job_status(state,job);
}
Item scene3d_navigation_operate(Scene3dNavigation* state,DomElement* root,Item request) {
    RootFrame roots(1);Rooted<Item> rooted(roots,request);auto read=ItemReader(rooted.get().to_const()).asMap();
    auto fail=[](const char* message){return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,message,nullptr));};
    const char* operation=navigation_text(read.get("operation"));char diagnostic[256]={};bool valid=scene3d_navigation_prepare(state,root,diagnostic,sizeof(diagnostic));
    if(operation&&!strncmp(operation,"navigation_bake_",16))return nav_bake_operate(state,root,read,operation,valid,diagnostic);
    if(operation&&!strcmp(operation,"navigation_read")) {
        RootedValueBuilder result,assets(true);
        for(auto* asset=state->assets;asset;asset=asset->next) {
            RootedValueBuilder value;value.put("id",Item{.item=s2it(heap_create_name(asset->id))});value.put("generation",Item{.item=i2it((int64_t)asset->generation)});
            value.put("revision",Item{.item=i2it(asset->revision)});value.put("ready",Item{.item=b2it(nav_asset_ready(asset))});
            value.put("link_count",Item{.item=i2it((int64_t)asset->compiled.link_bindings.size())});assets.append(value.value.get());
        }
        result.put("assets",assets.value.get());result.put("compilations",Item{.item=i2it((int64_t)state->stats.compilations)});
        result.put("invalidations",Item{.item=i2it((int64_t)state->stats.invalidations)});result.put("queries",Item{.item=i2it((int64_t)state->stats.queries)});
        result.put("nodes",Item{.item=i2it((int64_t)state->stats.nodes)});result.put("edges",Item{.item=i2it((int64_t)state->stats.edges)});
        result.put("vertices",Item{.item=i2it((int64_t)state->stats.vertices)});
        result.put("storage_nodes",Item{.item=i2it((int64_t)state->stats.storage_nodes)});
        result.put("rejected",Item{.item=b2it(!valid)});result.put("diagnostic",valid?ItemNull:Item{.item=s2it(heap_create_name(diagnostic))});return result.value.get();
    }
    const char* id=navigation_text(read.get("asset"));Scene3dNavigationAsset* selected=nullptr;
    if(id)for(auto* asset=state->assets;asset;asset=asset->next)if(!strcmp(asset->id,id)){selected=asset;break;}
    auto generation=read.get("generation");
    // generations are opaque integers: conversion through double could alias adjacent int64 values.
    bool current=selected&&nav_asset_ready(selected)&&generation.isInt()&&generation.asInt()>0&&(uint64_t)generation.asInt()==selected->generation;
    if(operation&&!strcmp(operation,"navigation_current"))return Item{.item=b2it(valid&&current)};
    if(!valid)return fail(diagnostic);
    if(!selected)return fail("navigation: unknown scene asset");
    if(!nav_asset_ready(selected))return fail("navigation: source bake has no published current domain");
    if(!generation.isNull()&&!current)return fail("navigation: stale asset generation");
    if(!operation||strcmp(operation,"navigation_path"))return fail("navigation: unsupported scene operation");
    state->stats.queries++;RootedValueBuilder result(navigation_path_value(selected->compiled,read.get("start").item(),read.get("goal").item(),read.get("options").item()));
    if(get_type_id(result.value.get())==LMD_TYPE_ERROR)return result.value.get();
    result.put("asset",Item{.item=s2it(heap_create_name(selected->id))});result.put("generation",Item{.item=i2it((int64_t)selected->generation)});
    result.put("revision",Item{.item=i2it(selected->revision)});return result.value.get();
}
