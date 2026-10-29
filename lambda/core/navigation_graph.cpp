#include "navigation_graph.hpp"
#include "../../lib/hash.h"
#include <math.h>
#include <float.h>
#include <string.h>

static const char* navigation_tags[]={"navigation-graph","navigation-grid","navigation-mesh","navigation-bake"};
NavigationKind navigation_kind(const char* text) {
    if(text)for(unsigned i=0;i<NAVIGATION_INVALID;i++)
        if(!strcmp(text,navigation_tags[i])||!strcmp(text,navigation_tags[i]+11))return (NavigationKind)i;
    return NAVIGATION_INVALID;
}
const char* navigation_kind_tag(NavigationKind kind){return (unsigned)kind<NAVIGATION_INVALID?navigation_tags[kind]:nullptr;}

const char* navigation_text(ItemReader value) {
    const char* text=value.isString()?value.asString()->chars:value.isSymbol()&&symbol_is_lambda_name(value.asSymbol())?value.asSymbol()->chars:nullptr;
    size_t length=value.isString()?value.asString()->len:value.isSymbol()?value.asSymbol()->len:0;
    return text&&strlen(text)==length?text:nullptr;
}
bool navigation_number(ItemReader value,double fallback,double maximum,double* number) {
    *number=value.isNull()?fallback:value.isInt()?(double)value.asInt():value.isFloat()?value.asFloat():NAN;
    return isfinite(*number)&&*number>=0&&*number<=maximum;
}
bool navigation_boolean(ItemReader value,bool fallback,bool* result) {
    if(!value.isNull()&&!value.isBool())return false;*result=value.isNull()?fallback:value.asBool();return true;
}
static bool nav_fail(NavigationCompiled* compiled,const char* message) {
    strncpy(compiled->diagnostic,message,sizeof(compiled->diagnostic)-1);return false;
}
ItemReader navigation_field(ItemReader source,const char* key) {
    return source.isElement()?source.asElement().get_attr(key):source.asMap().get(key);
}
bool navigation_integer(ItemReader value,double fallback,size_t maximum,size_t* result) {
    double number;if(!navigation_number(value,fallback,(double)maximum,&number)||floor(number)!=number)return false;
    *result=(size_t)number;return true;
}
static bool nav_mask(ItemReader value,uint32_t* result) {
    size_t number;if(!navigation_integer(value,UINT32_MAX,UINT32_MAX,&number))return false;*result=(uint32_t)number;return true;
}
bool navigation_vector(ItemReader value,unsigned dimensions,NavigationVec fallback,NavigationVec* result) {
    if(value.isNull()){*result=fallback;return true;}
    if(!value.isArray()||value.asArray().length()!=dimensions)return false;
    double values[3]={};
    for(unsigned i=0;i<dimensions;i++) {
        auto component=value.asArray().get(i);values[i]=component.isInt()?(double)component.asInt():component.isFloat()?component.asFloat():NAN;
        if(!isfinite(values[i]))return false;
    }
    *result={values[0],values[1],values[2]};return true;
}
uint64_t navigation_reference(const NavigationCompiled& compiled,ItemReader value) {
    if(compiled.kind==NAVIGATION_GRID) {
        if(!value.isArray()||value.asArray().length()!=compiled.dimensions)return 0;
        size_t cell[3]={};
        for(unsigned i=0;i<compiled.dimensions;i++)if(!navigation_integer(value.asArray().get(i),NAN,compiled.size[i]-1,&cell[i]))return 0;
        return 1+cell[0]+compiled.size[0]*(cell[1]+compiled.size[1]*cell[2]);
    }
    const char* text=navigation_text(value);NavigationBinding key={};
    if(!text||!*text||strlen(text)>=sizeof(key.name))return 0;strcpy(key.name,text);
    const auto* found=compiled.ids.get(key);return found?found->id:0;
}
static bool nav_graph(NavigationCompiled* compiled,ItemReader source) {
    auto node_list=navigation_field(source,"nodes"),edge_list=navigation_field(source,"edges");
    size_t dimensions;
    if(!navigation_integer(navigation_field(source,"dimensions"),2,3,&dimensions)||dimensions<2||!node_list.isArray()||
        node_list.asArray().length()<1||node_list.asArray().length()>NAVIGATION_MAX_NODES||
        (!edge_list.isNull()&&(!edge_list.isArray()||edge_list.asArray().length()>NAVIGATION_MAX_EDGES)))return nav_fail(compiled,"navigation: requires 2D/3D node/edge arrays within graph quotas");
    compiled->dimensions=(unsigned)dimensions;
    if(!compiled->ids.init((size_t)node_list.asArray().length()))return nav_fail(compiled,"navigation: identity allocation failed");
    lam::ArrayList<NavigationNodeDesc> nodes;lam::ArrayList<NavigationEdgeDesc> edges;
    for(int64_t i=0;i<node_list.asArray().length();i++) {
        auto input=node_list.asArray().get(i);auto read=[&](const char* key){return navigation_field(input,key);};
        if(!input.isMap())return nav_fail(compiled,"navigation: node descriptions must be maps");
        const char* name=navigation_text(read("id"));NavigationBinding binding={};NavigationNodeDesc node={};
        if(!input.isMap()||!name||!*name||strlen(name)>=sizeof(binding.name))return nav_fail(compiled,"navigation: each node requires a nonempty ID shorter than 128 bytes");
        strcpy(binding.name,name);binding.id=hash_fnv1a_64(name,strlen(name));if(!binding.id)binding.id=1;
        if(compiled->ids.get(binding))return nav_fail(compiled,"navigation: duplicate node ID");
        node.id=binding.id;
        if(read("position").isNull()||!navigation_vector(read("position"),compiled->dimensions,{},&node.position)||
            !nav_mask(read("groups"),&node.membership)||!navigation_boolean(read("disabled"),false,&node.disabled))return nav_fail(compiled,"navigation: invalid node position, groups or disabled flag");
        compiled->ids.set(binding);
        if(compiled->ids.oom()||!compiled->bindings.append(binding)||!nodes.append(node))return nav_fail(compiled,"navigation: node allocation failed");
    }
    auto append=[&](NavigationEdgeDesc edge){return edges.size()<NAVIGATION_MAX_EDGES&&edges.append(edge);};
    for(int64_t i=0;i<edge_list.asArray().length();i++) {
        auto input=edge_list.asArray().get(i);auto read=[&](const char* key){return navigation_field(input,key);};
        if(!input.isMap())return nav_fail(compiled,"navigation: edge descriptions must be maps");
        NavigationEdgeDesc edge={};bool bidirectional;
        edge.from=navigation_reference(*compiled,read("from"));edge.to=navigation_reference(*compiled,read("to"));
        if(!input.isMap()||!edge.from||!edge.to||!navigation_number(read("cost"),1,DBL_MAX,&edge.cost)||
            !nav_mask(read("groups"),&edge.membership)||!navigation_boolean(read("disabled"),false,&edge.disabled)||
            !navigation_boolean(read("bidirectional"),false,&bidirectional))return nav_fail(compiled,"navigation: invalid edge reference, cost, groups or flag");
        auto guards=read("guards");
        if(!guards.isNull()) {
            if(!guards.isArray()||guards.asArray().length()>NAVIGATION_MAX_GUARDS)return nav_fail(compiled,"navigation: edge requires at most six clearance-node IDs");
            for(int64_t k=0;k<guards.asArray().length();k++) {
                uint64_t id=navigation_reference(*compiled,guards.asArray().get(k));if(!id)return nav_fail(compiled,"navigation: unknown clearance-node ID");edge.guards[edge.guard_count++]=id;
            }
        }
        if(!append(edge))return nav_fail(compiled,"navigation: edge quota or allocation failed");
        if(bidirectional){uint64_t from=edge.from;edge.from=edge.to;edge.to=from;if(!append(edge))return nav_fail(compiled,"navigation: edge quota or allocation failed");}
    }
    NavigationDesc desc={compiled->dimensions,nodes.data(),nodes.size(),edges.data(),edges.size()};NavigationError error={};
    return navigation_create(&desc,&compiled->graph,&error)||nav_fail(compiled,error.message);
}
static bool nav_grid(NavigationCompiled* compiled,ItemReader source) {
    auto size=navigation_field(source,"size");
    if(!size.isArray()||(size.asArray().length()!=2&&size.asArray().length()!=3))return nav_fail(compiled,"navigation: grid size requires two or three cell counts");
    compiled->dimensions=(unsigned)size.asArray().length();
    size_t cell_total=1;
    for(unsigned i=0;i<compiled->dimensions;i++) {
        if(!navigation_integer(size.asArray().get(i),NAN,NAVIGATION_MAX_NODES,&compiled->size[i])||!compiled->size[i]||
            compiled->size[i]>NAVIGATION_MAX_NODES/cell_total)return nav_fail(compiled,"navigation: invalid grid cell count/quota");cell_total*=compiled->size[i];
    }
    NavigationGridDesc desc={};desc.dimensions=compiled->dimensions;memcpy(desc.size,compiled->size,sizeof(desc.size));
    if(!navigation_vector(navigation_field(source,"origin"),compiled->dimensions,{},&desc.origin)||
        !navigation_vector(navigation_field(source,"cell_size"),compiled->dimensions,{1,1,1},&desc.cell_size)||
        !navigation_boolean(navigation_field(source,"diagonals"),false,&desc.diagonals))return nav_fail(compiled,"navigation: invalid grid origin, cell size or diagonals flag");
    if(compiled->dimensions==2)desc.cell_size.z=1;
    lam::ArrayList<double> costs;lam::ArrayList<uint32_t> memberships;lam::ArrayList<bool> blocked;
    auto cost_list=navigation_field(source,"costs"),group_list=navigation_field(source,"groups"),blocked_list=navigation_field(source,"blocked");
    auto flat=[&](ItemReader value){return value.isNull()||(value.isArray()&&(size_t)value.asArray().length()==cell_total);};
    if(!flat(cost_list)||!flat(group_list)||(!blocked_list.isNull()&&!blocked_list.isArray()))return nav_fail(compiled,"navigation: grid costs/groups require flat cell arrays; blocked requires cell coordinates");
    for(size_t i=0;i<cell_total;i++) {
        double cost;uint32_t groups;
        if(!navigation_number(cost_list.isNull()?ItemReader():cost_list.asArray().get((int64_t)i),1,DBL_MAX,&cost)||
            !nav_mask(group_list.isNull()?ItemReader():group_list.asArray().get((int64_t)i),&groups))return nav_fail(compiled,"navigation: invalid grid cell cost/groups");
        if(!costs.append(cost)||!memberships.append(groups)||!blocked.append(false))return nav_fail(compiled,"navigation: grid allocation failed");
    }
    if(!blocked_list.isNull()) {
        if((size_t)blocked_list.asArray().length()>cell_total)return nav_fail(compiled,"navigation: blocked-cell quota exceeded");
        for(int64_t i=0;i<blocked_list.asArray().length();i++) {
            uint64_t id=navigation_reference(*compiled,blocked_list.asArray().get(i));if(!id)return nav_fail(compiled,"navigation: invalid blocked-cell coordinates");blocked[(size_t)id-1]=true;
        }
    }
    desc.costs=costs.data();desc.memberships=memberships.data();desc.blocked=blocked.data();NavigationError error={};
    return navigation_create_grid(&desc,&compiled->graph,&error)||nav_fail(compiled,error.message);
}
static bool nav_mesh(NavigationCompiled* compiled,ItemReader source) {
    auto vertex_list=navigation_field(source,"vertices"),triangle_list=navigation_field(source,"triangles");
    if(!vertex_list.isArray()||vertex_list.asArray().length()>NAVIGATION_MAX_VERTICES||
        !triangle_list.isArray()||triangle_list.asArray().length()>NAVIGATION_MAX_NODES||
        ((vertex_list.asArray().length()||triangle_list.asArray().length())&&(vertex_list.asArray().length()<3||!triangle_list.asArray().length())))
        return nav_fail(compiled,"navigation: surface requires bounded vertex/triangle arrays");
    size_t dimensions;auto first=vertex_list.asArray().get(0);double slope,radius;
    if((vertex_list.asArray().length()&&!first.isArray())||
        !navigation_integer(navigation_field(source,"dimensions"),vertex_list.asArray().length()?(double)first.asArray().length():2,3,&dimensions)||dimensions<2||
        !navigation_number(navigation_field(source,"max_slope_degrees"),45,90,&slope)||slope>=90||
        !navigation_number(navigation_field(source,"radius"),0,0,&radius))return nav_fail(compiled,"navigation: invalid surface dimension/slope; positive radius requires clearance baking");
    compiled->dimensions=(unsigned)dimensions;NavigationMeshDesc desc={};desc.dimensions=compiled->dimensions;desc.max_slope=slope*acos(-1.0)/180;
    if(!navigation_boolean(navigation_field(source,"two_sided"),false,&desc.two_sided))return nav_fail(compiled,"navigation: invalid two_sided surface flag");
    lam::ArrayList<NavigationVec> vertices;lam::ArrayList<NavigationTriangleDesc> triangles;
    for(int64_t i=0;i<vertex_list.asArray().length();i++) {
        NavigationVec p;auto value=vertex_list.asArray().get(i);
        if(value.isNull()||!navigation_vector(value,compiled->dimensions,{},&p))return nav_fail(compiled,"navigation: surface vertices require finite coordinate arrays");
        if(!vertices.append(p))return nav_fail(compiled,"navigation: surface vertex allocation failed");
    }
    for(int64_t i=0;i<triangle_list.asArray().length();i++) {
        auto value=triangle_list.asArray().get(i),indices=value.isMap()?value.asMap().get("vertices"):value;NavigationTriangleDesc triangle={};
        auto field=[&](const char* key){return value.isMap()?value.asMap().get(key):ItemReader();};
        if(!indices.isArray()||indices.asArray().length()!=3||!navigation_number(field("cost"),1,DBL_MAX,&triangle.cost)||
            !nav_mask(field("groups"),&triangle.membership)||!navigation_boolean(field("disabled"),false,&triangle.disabled))return nav_fail(compiled,"navigation: surface triangle requires three vertex indices and valid area cost/groups/disabled");
        for(unsigned j=0;j<3;j++) {
            size_t index;if(!navigation_integer(indices.asArray().get(j),NAN,vertices.size()-1,&index))return nav_fail(compiled,"navigation: invalid surface triangle index");
            triangle.vertices[j]=(uint32_t)index;
        }
        if(!triangles.append(triangle))return nav_fail(compiled,"navigation: surface triangle allocation failed");
    }
    desc.vertices=vertices.data();desc.vertex_count=vertices.size();desc.triangles=triangles.data();desc.triangle_count=triangles.size();NavigationError error={};
    if(!navigation_mesh_create(&desc,&compiled->mesh,&error))return nav_fail(compiled,error.message);
    // the mesh owns its immutable graph; the common query/statistics pointer borrows it.
    compiled->graph=const_cast<NavigationGraph*>(navigation_mesh_graph(compiled->mesh));return true;
}
bool navigation_bake_settings_read(ItemReader source,NavigationBakeSettings* settings) {
    if(!settings||!source.isMap())return false;NavigationBakeSettings staged={};double slope;
    auto field=[&](const char* name){return source.asMap().get(name);};
    auto representation=field("representation");const char* kind=navigation_text(representation);
    if(!representation.isNull()&&(!kind||(strcmp(kind,"cells")&&strcmp(kind,"polygons"))))return false;
    staged.representation=kind&&!strcmp(kind,"polygons")?NAVIGATION_BAKE_POLYGONS:NAVIGATION_BAKE_CELLS;
    if(!field("low").isArray()||!field("high").isArray()||!navigation_vector(field("low"),3,{},&staged.low)||!navigation_vector(field("high"),3,{},&staged.high))return false;
    struct Number {const char* name;double fallback;double* output;};
    Number numbers[]={{"cell_size",.25,&staged.cell_size},{"cell_height",.05,&staged.cell_height},{"radius",.25,&staged.radius},
        {"height",1.8,&staged.height},{"max_step",.3,&staged.max_step},{"max_slope_degrees",45,&slope}};
    for(auto n:numbers)if(!navigation_number(field(n.name),n.fallback,DBL_MAX,n.output))return false;
    // compiled assets preserve radians directly, avoiding a lossy degrees round trip.
    if(!field("max_slope_radians").isNull()) {
        if(!navigation_number(field("max_slope_radians"),0,DBL_MAX,&staged.max_slope))return false;
    }else staged.max_slope=slope*acos(-1.0)/180;
    struct Limit {const char* name;size_t maximum;size_t* output;};
    Limit limits[]={{"max_cells",NAVIGATION_MAX_NODES,&staged.max_cells},{"max_fragments",NAVIGATION_BAKE_MAX_FRAGMENTS,&staged.max_fragments},{"max_work",NAVIGATION_BAKE_MAX_WORK,&staged.max_work}};
    for(auto limit:limits)if(!navigation_integer(field(limit.name),(double)limit.maximum,limit.maximum,limit.output))return false;
    *settings=staged;return true;
}
static bool nav_bake(NavigationCompiled* compiled,ItemReader source) {
    NavigationBakeDesc desc={};auto list=navigation_field(source,"triangles");lam::ArrayList<NavigationBakeTriangle> triangles;
    if(!navigation_bake_settings_read(navigation_field(source,"settings"),&desc.settings)||!navigation_bake_settings_hash(&desc.settings,compiled->settings_hash))
        return nav_fail(compiled,"navigation: invalid bake settings");
    auto format=navigation_field(source,"format");const char* encoding=navigation_text(format);
    if(!format.isNull()) {
        const char* frame=navigation_text(navigation_field(source,"frame")),*source_hash=navigation_text(navigation_field(source,"source_hash")),*settings_hash=navigation_text(navigation_field(source,"settings_hash"));
        auto cells=navigation_field(source,"cells");
        if(!encoding||strcmp(encoding,"cells")||!frame||strcmp(frame,"y_up")||!source_hash||strlen(source_hash)!=64||strspn(source_hash,"0123456789abcdef")!=64||
            !settings_hash||strcmp(settings_hash,compiled->settings_hash)||!cells.isArray()||cells.asArray().length()>NAVIGATION_MAX_NODES||!list.isNull())
            return nav_fail(compiled,"navigation: invalid stored bake format, frame or fingerprints");
        lam::ArrayList<NavigationBakeCell> values;
        for(int64_t i=0;i<cells.asArray().length();i++) {
            auto value=cells.asArray().get(i).asMap();NavigationBakeCell cell={};NavigationVec point;
            if(!navigation_integer(value.get("x"),NAN,NAVIGATION_MAX_NODES,&cell.x)||!navigation_integer(value.get("z"),NAN,NAVIGATION_MAX_NODES,&cell.z)||
                !navigation_vector(value.get("levels"),2,{NAN,NAN,0},&point)||!navigation_number(value.get("cost"),1,DBL_MAX,&cell.cost)||!nav_mask(value.get("groups"),&cell.groups))
                return nav_fail(compiled,"navigation: invalid stored bake cell");
            cell.floor=point.x;cell.ceiling=point.y;if(!values.append(cell))return nav_fail(compiled,"navigation: stored bake allocation failed");
        }
        NavigationError error={};if(!navigation_bake_from_cells(&desc.settings,values.data(),values.size(),&compiled->bake,&error))return nav_fail(compiled,error.message);
        strcpy(compiled->source_hash,source_hash);compiled->dimensions=3;compiled->graph=const_cast<NavigationGraph*>(navigation_bake_graph(compiled->bake));return true;
    }
    if(!list.isArray()||list.asArray().length()>NAVIGATION_BAKE_MAX_TRIANGLES)
        return nav_fail(compiled,"navigation: bake requires settings and a bounded triangle array");
    for(int64_t i=0;i<list.asArray().length();i++) {
        auto value=list.asArray().get(i),vertices=value.isMap()?value.asMap().get("vertices"):value;NavigationBakeTriangle triangle={};
        if(!vertices.isArray()||vertices.asArray().length()!=3||!nav_mask(value.asMap().get("groups"),&triangle.groups)||
            !navigation_number(value.asMap().get("cost"),1,DBL_MAX,&triangle.cost)||!navigation_boolean(value.asMap().get("walkable"),true,&triangle.walkable))
            return nav_fail(compiled,"navigation: invalid bake triangle metadata");
        for(unsigned j=0;j<3;j++)if(!vertices.asArray().get(j).isArray()||!navigation_vector(vertices.asArray().get(j),3,{},&triangle.vertices[j]))
            return nav_fail(compiled,"navigation: bake triangles require three finite XYZ vertices");
        if(!triangles.append(triangle))return nav_fail(compiled,"navigation: bake triangle allocation failed");
    }
    desc.triangles=triangles.data();desc.triangle_count=triangles.size();NavigationError error={};
    if(!navigation_bake_create(&desc,&compiled->bake,&error))return nav_fail(compiled,error.message);
    compiled->dimensions=3;compiled->graph=const_cast<NavigationGraph*>(navigation_bake_graph(compiled->bake));return true;
}
bool navigation_links_read(NavigationCompiled* compiled,ItemReader source) {
    auto list=navigation_field(source,"links");
    if((!list.isNull()&&!list.isArray())||list.asArray().length()>NAVIGATION_MAX_LINKS||
        !navigation_integer(navigation_field(source,"max_attachment_tests"),NAVIGATION_MAX_ATTACHMENT_TESTS,NAVIGATION_MAX_ATTACHMENT_TESTS,&compiled->max_attachment_tests))
        return nav_fail(compiled,"navigation: links require a bounded array and attachment budget");
    if(list.asArray().length()&&compiled->kind!=NAVIGATION_MESH&&compiled->kind!=NAVIGATION_BAKE)return nav_fail(compiled,"navigation: explicit links require a surface or baked domain");
    for(int64_t i=0;i<list.asArray().length();i++) {
        auto value=list.asArray().get(i);auto field=[&](const char* key){return value.asMap().get(key);};
        NavigationLinkBinding link={};const char* name=navigation_text(field("id")),*kind=field("kind").isNull()?"link":navigation_text(field("kind"));
        if(!value.isMap()||!name||!*name||strlen(name)>=sizeof(link.binding.name)||!kind||!*kind||strlen(kind)>=sizeof(link.kind))return nav_fail(compiled,"navigation: each link requires an ID/kind within name quotas");
        for(const auto& before:compiled->link_bindings)if(!strcmp(before.binding.name,name))return nav_fail(compiled,"navigation: duplicate link ID");
        strcpy(link.binding.name,name);strcpy(link.kind,kind);link.binding.id=link.description.id=(uint64_t)i+1;auto& desc=link.description;
        desc.automatic_cost=field("cost").isNull();
        if(field("start").isNull()||field("end").isNull()||!navigation_vector(field("start"),compiled->dimensions,{},&desc.start)||
            !navigation_vector(field("end"),compiled->dimensions,{},&desc.end)||!navigation_number(field("cost"),0,DBL_MAX,&desc.cost)||
            !navigation_number(field("width"),1,DBL_MAX,&desc.width)||!navigation_number(field("height"),DBL_MAX,DBL_MAX,&desc.height)||
            !navigation_number(field("duration"),0,DBL_MAX,&desc.duration)||!navigation_number(field("max_projection_distance"),1,DBL_MAX,&desc.max_projection_distance)||
            !nav_mask(field("groups"),&desc.membership)||!navigation_boolean(field("disabled"),false,&desc.disabled)||!navigation_boolean(field("bidirectional"),false,&desc.bidirectional))
            return nav_fail(compiled,"navigation: invalid link endpoints, cost, clearance or flags");
        const char* pins[]={"start_polygon","end_polygon"};uint64_t* outputs[]={&desc.start_polygon,&desc.end_polygon};
        for(unsigned k=0;k<2;k++)if(!field(pins[k]).isNull()) {
            size_t index;if(!navigation_integer(field(pins[k]),NAN,NAVIGATION_MAX_NODES-1,&index))return nav_fail(compiled,"navigation: link polygon pins must be zero-based integers");*outputs[k]=index+1;
        }
        NavigationError error={};if(!navigation_link_valid(&desc,compiled->dimensions,&error))return nav_fail(compiled,error.message);
        if(!compiled->link_bindings.append(link))return nav_fail(compiled,"navigation: link description allocation failed");
    }
    return true;
}
bool navigation_link_declarations_equal(const NavigationCompiled& a,const NavigationCompiled& b) {
    if(a.max_attachment_tests!=b.max_attachment_tests||a.link_bindings.size()!=b.link_bindings.size())return false;
    for(size_t i=0;i<a.link_bindings.size();i++) {
        const auto &x=a.link_bindings[i],&y=b.link_bindings[i];
        if(strcmp(x.binding.name,y.binding.name)||strcmp(x.kind,y.kind)||!navigation_link_equal(&x.description,&y.description))return false;
    }
    return true;
}
bool navigation_build_link_overlay(const NavigationCompiled& compiled,const NavigationSurfaceView& view,NavigationLinks** links,NavigationError* error) {
    *links=nullptr;if(compiled.link_bindings.empty())return true;lam::ArrayList<NavigationLinkDesc> descriptions;
    for(const auto& link:compiled.link_bindings)if(!descriptions.append(link.description)){if(error)*error={"navigation: link description allocation failed",0};return false;}
    NavigationLinksDesc desc={descriptions.data(),descriptions.size(),compiled.max_attachment_tests};return navigation_links_create(&view,&desc,links,error);
}
bool navigation_attach_links(NavigationCompiled* compiled) {
    if(!compiled)return false;NavigationLinks* links=nullptr;NavigationSurfaceView view={};
    if(!compiled->link_bindings.empty()) {
        bool surface=compiled->mesh?navigation_mesh_surface(compiled->mesh,&view):navigation_bake_surface(compiled->bake,&view);
        NavigationError error={};if(!surface||!navigation_build_link_overlay(*compiled,view,&links,&error))return nav_fail(compiled,error.message?error.message:"navigation: missing link surface");
    }
    navigation_links_destroy(compiled->links);compiled->links=links;
    if(links)compiled->graph=const_cast<NavigationGraph*>(navigation_links_graph(links));
    else if(compiled->mesh)compiled->graph=const_cast<NavigationGraph*>(navigation_mesh_graph(compiled->mesh));
    else if(compiled->bake)compiled->graph=const_cast<NavigationGraph*>(navigation_bake_graph(compiled->bake));
    return true;
}
static bool nav_query_error(NavigationError* error,const char* message){if(error)*error={message,0};return false;}
bool navigation_search_read(const NavigationCompiled& compiled,ItemReader options,NavigationQuery* query,NavigationError* error) {
    if(!query||(!options.isNull()&&!options.isMap()))return nav_query_error(error,"navigation: path options require a map");
    auto field=[&](const char* key){return options.asMap().get(key);};NavigationQuery result={};size_t filter;
    if(!navigation_integer(field("max_expansions"),(double)navigation_node_count(compiled.graph),NAVIGATION_MAX_NODES,&result.max_expansions)||
        !navigation_integer(field("max_edge_tests"),(double)navigation_edge_count(compiled.graph),NAVIGATION_MAX_EDGES,&result.max_edge_tests)||
        !navigation_integer(field("filter"),UINT32_MAX,UINT32_MAX,&filter)||!navigation_boolean(field("partial"),false,&result.allow_partial))
        return nav_query_error(error,"navigation: invalid search budget, filter or partial flag");
    auto value=field("heuristic");const char* heuristic=navigation_text(value);
    if(!value.isNull()&&(!heuristic||(strcmp(heuristic,"zero")&&strcmp(heuristic,"euclidean"))))return nav_query_error(error,"navigation: heuristic must be zero or euclidean");
    result.filter=(uint32_t)filter;result.euclidean=!heuristic||!strcmp(heuristic,"euclidean");*query=result;return true;
}
bool navigation_surface_read(const NavigationCompiled& compiled,ItemReader options,NavigationSurfaceQuery* query,NavigationError* error) {
    if(!query)return nav_query_error(error,"navigation: missing surface query output");
    NavigationSurfaceQuery result={};if(!navigation_search_read(compiled,options,&result.path.search,error))return false;
    auto field=[&](const char* key){return options.asMap().get(key);};NavigationBakeSettings profile={};
    if(compiled.bake)navigation_bake_settings(compiled.bake,&profile);
    const char* assembly_budget=compiled.bake?"max_waypoint_tests":"max_portal_tests";
    if((compiled.links||(compiled.bake&&profile.representation==NAVIGATION_BAKE_POLYGONS))&&!field("max_portal_tests").isNull())assembly_budget="max_portal_tests";
    if(!navigation_number(field("max_projection_distance"),1,DBL_MAX,&result.path.max_projection_distance)||
        !navigation_integer(field("max_projection_tests"),2.0*navigation_node_count(compiled.graph),2*NAVIGATION_MAX_NODES,&result.path.max_projection_tests)||
        !navigation_integer(field(assembly_budget),32.0*navigation_node_count(compiled.graph)+16,16*NAVIGATION_MAX_EDGES,&result.path.max_portal_tests)||
        !navigation_number(field("radius"),profile.radius,profile.radius,&result.radius))return nav_query_error(error,"navigation: invalid surface budget or incompatible agent radius");
    double height_max=compiled.bake?profile.height:DBL_MAX;
    if(!navigation_number(field("height"),profile.height,height_max,&result.height)||!navigation_number(field("max_step"),profile.max_step,DBL_MAX,&result.max_step))
        return nav_query_error(error,"navigation: invalid agent height/step profile");
    *query=result;return true;
}
bool navigation_surface_find_compiled(const NavigationCompiled& compiled,const NavigationSurfaceQuery* query,NavigationMeshPath* path,NavigationError* error) {
    if(compiled.links)return navigation_links_find(compiled.links,query,path,error);
    NavigationSurfaceView view={};bool valid=compiled.bake?navigation_bake_surface(compiled.bake,&view):navigation_mesh_surface(compiled.mesh,&view);
    return valid?navigation_surface_find(&view,query,path,error):nav_query_error(error,"navigation: a compiled surface is required");
}
bool navigation_compile(NavigationCompiled* compiled,ItemReader source) {
    if(!compiled||( !source.isElement()&&!source.isMap()))return compiled&&nav_fail(compiled,"navigation: requires a navigation graph/grid/mesh description");
    const char* type=source.isElement()?source.asElement().tagName():navigation_text(source.asMap().get("type"));
    if(source.isElement()&&source.asElement().childCount())return nav_fail(compiled,"navigation: assets have no child elements");
    double version;if(!navigation_number(navigation_field(source,"version"),1,1,&version)||version!=1)return nav_fail(compiled,"navigation: requires schema version 1");
    compiled->kind=navigation_kind(type);
    bool (*compilers[])(NavigationCompiled*,ItemReader)={nav_graph,nav_grid,nav_mesh,nav_bake};
    if(compiled->kind>=NAVIGATION_INVALID)return nav_fail(compiled,"navigation: expected a navigation graph/grid/mesh/bake");
    return compilers[compiled->kind](compiled,source)&&navigation_links_read(compiled,source)&&navigation_attach_links(compiled);
}
bool navigation_compiled_equal(const NavigationCompiled& a,const NavigationCompiled& b) {
    if(a.dimensions!=b.dimensions||a.kind!=b.kind||!navigation_link_declarations_equal(a,b)||!navigation_links_equal(a.links,b.links))return false;
    if(a.kind==NAVIGATION_MESH)return navigation_mesh_equal(a.mesh,b.mesh);
    if(a.kind==NAVIGATION_BAKE)return !strcmp(a.source_hash,b.source_hash)&&!strcmp(a.settings_hash,b.settings_hash)&&navigation_bake_equal(a.bake,b.bake);
    if(a.bindings.size()!=b.bindings.size()||!navigation_equal(a.graph,b.graph))return false;
    for(unsigned i=0;i<3;i++)if(a.size[i]!=b.size[i])return false;
    for(size_t i=0;i<a.bindings.size();i++)if(a.bindings[i].id!=b.bindings[i].id||strcmp(a.bindings[i].name,b.bindings[i].name))return false;
    return true;
}
