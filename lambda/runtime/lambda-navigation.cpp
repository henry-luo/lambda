#include "lambda-navigation.hpp"
#include "value_builder.hpp"
#include "lambda-number-runtime.hpp"
#include "lambda-error.h"
#include <math.h>
#include <float.h>

static Item nav_error(const char* message){return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,message,nullptr));}
static Item nav_string(const char* text){return Item{.item=s2it(heap_create_name(text))};}
static Item nav_point_value(NavigationVec p,unsigned dimensions) {
    RootedValueBuilder point(true);point.append(push_d(p.x));point.append(push_d(p.y));if(dimensions==3)point.append(push_d(p.z));return point.value.get();
}
Item navigation_settings_value(const NavigationBakeSettings& settings) {
    RootedValueBuilder profile(RootedValueBuilder::DATA_MAP);
    profile.put("radius",push_d(settings.radius));profile.put("height",push_d(settings.height));profile.put("max_step",push_d(settings.max_step));
    profile.put("cell_size",push_d(settings.cell_size));profile.put("cell_height",push_d(settings.cell_height));
    profile.put("version",Item{.item=i2it(1)});profile.put("frame",nav_string("y_up"));
    profile.put("representation",nav_string(settings.representation==NAVIGATION_BAKE_POLYGONS?"polygons":"cells"));
    profile.put("low",nav_point_value(settings.low,3));profile.put("high",nav_point_value(settings.high,3));
    profile.put("max_slope_degrees",push_d(settings.max_slope*180/acos(-1.0)));profile.put("max_slope_radians",push_d(settings.max_slope));
    profile.put("max_cells",Item{.item=i2it((int64_t)settings.max_cells)});profile.put("max_fragments",Item{.item=i2it((int64_t)settings.max_fragments)});
    profile.put("max_work",Item{.item=i2it((int64_t)settings.max_work)});return profile.value.get();
}
static Item nav_links_value(const NavigationCompiled& compiled) {
    RootedValueBuilder result(true);
    for(const auto& binding:compiled.link_bindings) {
        const auto& link=binding.description;RootedValueBuilder value(RootedValueBuilder::DATA_MAP);
        value.put("id",nav_string(binding.binding.name));value.put("kind",nav_string(binding.kind));
        value.put("start",nav_point_value(link.start,compiled.dimensions));value.put("end",nav_point_value(link.end,compiled.dimensions));
        value.put("cost",link.automatic_cost?ItemNull:push_d(link.cost));value.put("width",push_d(link.width));value.put("height",push_d(link.height));
        value.put("duration",push_d(link.duration));value.put("max_projection_distance",push_d(link.max_projection_distance));
        value.put("groups",Item{.item=i2it(link.membership)});value.put("disabled",Item{.item=b2it(link.disabled)});value.put("bidirectional",Item{.item=b2it(link.bidirectional)});
        value.put("start_polygon",link.start_polygon?Item{.item=i2it((int64_t)link.start_polygon-1)}:ItemNull);
        value.put("end_polygon",link.end_polygon?Item{.item=i2it((int64_t)link.end_polygon-1)}:ItemNull);result.append(value.value.get());
    }
    return result.value.get();
}
Item navigation_bake_value(const NavigationBake* bake,const char* source_hash,const char* settings_hash,const NavigationCompiled* definitions) {
    NavigationBakeSettings settings={};NavigationBakeStats stats={};
    if(!navigation_bake_settings(bake,&settings)||!navigation_bake_stats(bake,&stats))return nav_error("navigation: missing compiled bake");
    RootedValueBuilder result(RootedValueBuilder::DATA_MAP),cells(true);result.put("type",nav_string("bake"));result.put("version",Item{.item=i2it(1)});
    result.put("format",nav_string("cells"));result.put("frame",nav_string("y_up"));result.put("source_hash",nav_string(source_hash));result.put("settings_hash",nav_string(settings_hash));
    result.put("settings",navigation_settings_value(settings));
    for(uint64_t id=1;id<=stats.cells;id++) {
        NavigationBakeCell cell={};navigation_bake_cell(bake,id,&cell);RootedValueBuilder value(RootedValueBuilder::DATA_MAP);
        value.put("x",Item{.item=i2it((int64_t)cell.x)});value.put("z",Item{.item=i2it((int64_t)cell.z)});
        value.put("levels",nav_point_value({cell.floor,cell.ceiling,0},2));value.put("cost",push_d(cell.cost));value.put("groups",Item{.item=i2it(cell.groups)});cells.append(value.value.get());
    }
    result.put("cells",cells.value.get());
    if(definitions){result.put("links",nav_links_value(*definitions));result.put("max_attachment_tests",Item{.item=i2it((int64_t)definitions->max_attachment_tests)});}
    return result.value.get();
}
extern "C" Item fn_navigation_validate(Item input) {
    GUARD_ERROR1(input);RootFrame roots(1);Rooted<Item> source(roots,input);NavigationCompiled compiled;
    if(!navigation_compile(&compiled,ItemReader(source.get().to_const())))return nav_error(compiled.diagnostic);
    return Item{.item=b2it(true)};
}
extern "C" Item fn_navigation_path(Item input,Item from,Item to,Item settings) {
    GUARD_ERROR2(input,from);GUARD_ERROR2(to,settings);RootFrame roots(4);Rooted<Item> source(roots,input),start(roots,from),goal(roots,to),options(roots,settings);
    NavigationCompiled compiled;if(!navigation_compile(&compiled,ItemReader(source.get().to_const())))return nav_error(compiled.diagnostic);
    return navigation_path_value(compiled,start.get(),goal.get(),options.get());
}
Item navigation_path_value(const NavigationCompiled& compiled,Item from,Item to,Item settings) {
    GUARD_ERROR3(from,to,settings);RootFrame roots(3);Rooted<Item> start(roots,from),goal(roots,to),options(roots,settings);
    ItemReader read(options.get().to_const());NavigationQuery query={};NavigationError error={};
    if(!navigation_search_read(compiled,read,&query,&error))return nav_error(error.message);
    struct PathOwner {
        NavigationPath graph={};NavigationMeshPath surface={};
        ~PathOwner(){navigation_path_clear(&graph);navigation_mesh_path_clear(&surface);}
    } owner;
    const NavigationPath* selected=&owner.graph;
    bool domain=compiled.mesh||compiled.bake;
    if(domain) {
        NavigationSurfaceQuery surface={};
        if(!navigation_surface_read(compiled,read,&surface,&error))return nav_error(error.message);
        if(ItemReader(start.get().to_const()).isNull()||ItemReader(goal.get().to_const()).isNull()||
            !navigation_vector(ItemReader(start.get().to_const()),compiled.dimensions,{},&surface.path.start)||
            !navigation_vector(ItemReader(goal.get().to_const()),compiled.dimensions,{},&surface.path.goal))return nav_error("navigation: invalid surface endpoint");
        if(!navigation_surface_find_compiled(compiled,&surface,&owner.surface,&error))return nav_error(error.message);
        selected=&owner.surface.corridor;
    }else {
        query.start=navigation_reference(compiled,ItemReader(start.get().to_const()));query.goal=navigation_reference(compiled,ItemReader(goal.get().to_const()));
        if(!query.start||!query.goal)return nav_error("navigation: unknown node ID or invalid grid start/goal coordinates");
        if(!navigation_find(compiled.graph,&query,&owner.graph,&error))return nav_error(error.message);
    }
    const auto& path=*selected;const char* statuses[]={"complete","partial","unreachable","budget_exhausted"};
    RootedValueBuilder result,nodes(true),points(true);
    for(size_t i=0;i<path.count;i++) {
        uint64_t id=path.nodes[i];NavigationNodeDesc node={};if(!navigation_node(compiled.graph,id,&node))return nav_error("navigation: invalid path identity");
        if(compiled.links&&id>navigation_links_polygon_count(compiled.links))continue;
        if(domain)nodes.append(Item{.item=i2it((int64_t)(id-1))});
        else if(compiled.kind==NAVIGATION_GRID) {
            RootedValueBuilder coordinates(true);uint64_t cell=id-1;
            for(unsigned axis=0;axis<compiled.dimensions;axis++){coordinates.append(Item{.item=i2it((int64_t)(cell%compiled.size[axis]))});cell/=compiled.size[axis];}
            nodes.append(coordinates.value.get());
        }else {
            size_t index=navigation_node_index(compiled.graph,id);
            if(index>=compiled.bindings.size())return nav_error("navigation: missing path name");
            // copied names share the provider's immutable node order, avoiding a second identity search.
            nodes.append(nav_string(compiled.bindings[index].name));
        }
        if(!domain)points.append(nav_point_value(node.position,compiled.dimensions));
    }
    bool has_path=path.count!=0;double cost=path.cost;
    if(domain) {
        const auto& surface=owner.surface;RootedValueBuilder portals(true);
        for(size_t i=0;i<surface.point_count;i++)points.append(nav_point_value(surface.points[i],compiled.dimensions));
        for(size_t i=0;i<surface.portal_count;i++) {
            RootedValueBuilder portal(true);portal.append(nav_point_value(surface.portals[i].left,compiled.dimensions));
            portal.append(nav_point_value(surface.portals[i].right,compiled.dimensions));portals.append(portal.value.get());
        }
        has_path=surface.point_count!=0;cost=surface.travel_cost;
        result.put("corridor",nodes.value.get());result.put("portals",portals.value.get());
        result.put("corridor_cost",path.count?push_d(path.cost):ItemNull);result.put("length",has_path?push_d(surface.length):ItemNull);
        result.put("projected_start",surface.start_polygon?nav_point_value(surface.projected_start,compiled.dimensions):ItemNull);
        result.put("projected_goal",surface.goal_polygon?nav_point_value(surface.projected_goal,compiled.dimensions):ItemNull);
        result.put("path_end",has_path?nav_point_value(surface.path_end,compiled.dimensions):ItemNull);
        result.put("start_polygon",surface.start_polygon?Item{.item=i2it((int64_t)(surface.start_polygon-1))}:ItemNull);
        result.put("goal_polygon",surface.goal_polygon?Item{.item=i2it((int64_t)(surface.goal_polygon-1))}:ItemNull);
        result.put("start_distance",surface.start_polygon?push_d(surface.start_distance):ItemNull);
        result.put("goal_distance",surface.goal_polygon?push_d(surface.goal_distance):ItemNull);
        result.put("projection_tests",Item{.item=i2it((int64_t)surface.projection_tests)});result.put("portal_tests",Item{.item=i2it((int64_t)surface.portal_tests)});
        RootedValueBuilder traversals(true);
        for(size_t i=0;i<surface.traversal_count;i++) {
            const auto& action=surface.traversals[i];if(!action.id||action.id>compiled.link_bindings.size())return nav_error("navigation: invalid traversal identity");
            const auto& binding=compiled.link_bindings[(size_t)action.id-1];RootedValueBuilder value(RootedValueBuilder::DATA_MAP);
            value.put("id",nav_string(binding.binding.name));value.put("kind",nav_string(binding.kind));value.put("reverse",Item{.item=b2it(action.reverse)});
            value.put("start",nav_point_value(action.start,compiled.dimensions));value.put("end",nav_point_value(action.end,compiled.dimensions));
            value.put("start_point",Item{.item=i2it((int64_t)action.start_point)});value.put("end_point",Item{.item=i2it((int64_t)action.end_point)});
            value.put("cost",push_d(action.cost));value.put("duration",push_d(action.duration));traversals.append(value.value.get());
        }
        result.put("links",traversals.value.get());result.put("link_attachment_tests",Item{.item=i2it((int64_t)navigation_links_attachment_tests(compiled.links))});
        if(compiled.bake) {
            NavigationBakeSettings settings={};navigation_bake_settings(compiled.bake,&settings);
            bool polygons=settings.representation==NAVIGATION_BAKE_POLYGONS;
            result.put("profile",navigation_settings_value(settings));result.put("domain",nav_string(polygons?"voxel_polygons":"voxel_cells"));
            result.put("source_hash",*compiled.source_hash?nav_string(compiled.source_hash):ItemNull);result.put("settings_hash",nav_string(compiled.settings_hash));
            result.put("waypoint_tests",Item{.item=i2it((int64_t)surface.portal_tests)});
            if(polygons)result.put("region_count",Item{.item=i2it((int64_t)navigation_bake_region_count(compiled.bake))});
            else {
                result.put("start_cell",surface.start_polygon?Item{.item=i2it((int64_t)(surface.start_polygon-1))}:ItemNull);
                result.put("goal_cell",surface.goal_polygon?Item{.item=i2it((int64_t)(surface.goal_polygon-1))}:ItemNull);
            }
        }
    }
    result.put("status",nav_string(statuses[path.status]));result.put("partial",Item{.item=b2it(has_path&&path.status!=NAVIGATION_COMPLETE)});
    result.put("nodes",nodes.value.get());result.put("points",points.value.get());result.put("cost",has_path?push_d(cost):ItemNull);
    result.put("expanded",Item{.item=i2it((int64_t)path.expanded)});result.put("edge_tests",Item{.item=i2it((int64_t)path.edge_tests)});
    result.put("updates",Item{.item=i2it((int64_t)path.updates)});result.put("frontier_peak",Item{.item=i2it((int64_t)path.frontier_peak)});
    return result.value.get();
}
