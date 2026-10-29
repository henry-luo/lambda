#include "navigation.h"
#include <float.h>

namespace {
struct Span {double low=0,high=1;NavigationVec crossing={},end={};bool hit=false;};
struct Candidate {uint64_t polygon;size_t parent;Span span;};
struct MotionBuild {
    const NavigationSurfaceView& view;const NavigationSurfaceMove& query;
    NavigationSurfaceMotion& result;NavigationError* error;NavigationVec start,goal;
    NavigationMeshPath points={};
    MotionBuild(const NavigationSurfaceView& v,const NavigationSurfaceMove& q,NavigationSurfaceMotion& out,NavigationError* e)
        :view(v),query(q),result(out),error(e),start(out.start),goal(q.profile.path.goal){points.points=out.points;}
    bool fail(const char* message,uint64_t polygon=0){if(error)*error={message,polygon};return false;}
    bool append(NavigationVec point) {
        if(points.point_count==NAVIGATION_MOVE_MAX_POINTS)return fail("navigation motion: point quota exceeded");
        // the shared appender borrows this fixed output buffer; no owned path is cleared here.
        return navigation_waypoint_append(&points,point,0,error);
    }
    void finish(){result.point_count=points.point_count;result.length=points.length;}
    NavigationVec at(double t,const NavigationVec& anchor,const double normal[3])const {
        double x=start.x*(1-t)+goal.x*t,axis=navigation_surface_axis(start,view.dimensions)*(1-t)+navigation_surface_axis(goal,view.dimensions)*t;
        if(view.dimensions==2)return {x,axis,0};
        return {x,anchor.y-(normal[0]*(x-anchor.x)+normal[2]*(axis-anchor.z))/normal[1],axis};
    }
    bool clip(uint64_t polygon,double crossing,Span* span) {
        if(result.polygon_tests==query.max_polygon_tests)return false;result.polygon_tests++;
        NavigationVec vertices[NAVIGATION_SURFACE_MAX_VERTICES];size_t count=0;
        if(!view.polygon(view.owner,polygon,vertices,NAVIGATION_SURFACE_MAX_VERTICES,&count)||count<3||count>NAVIGATION_SURFACE_MAX_VERTICES)
            return fail("navigation motion: invalid convex polygon",polygon);
        for(size_t i=0;i<count;i++)if(!navigation_vec_finite(vertices[i]))return fail("navigation motion: nonfinite polygon",polygon);
        double winding=0,normal[3]={};size_t corner=2;
        for(;corner<count;corner++){winding=navigation_surface_turn(vertices[0],vertices[1],vertices[corner],view.dimensions);if(winding!=0)break;}
        if(!isfinite(winding)||winding==0||(view.dimensions==3&&(!navigation_triangle_normal(vertices[0],vertices[1],vertices[corner],normal)||normal[1]==0)))
            return fail("navigation motion: degenerate projected polygon",polygon);
        double sign=winding>0?1:-1;Span out;
        for(size_t i=0;i<count;i++) {
            auto a=vertices[i],b=vertices[(i+1)%count];
            double ex=b.x-a.x,ey=navigation_surface_axis(b,view.dimensions)-navigation_surface_axis(a,view.dimensions),
                sx=start.x-a.x,sy=navigation_surface_axis(start,view.dimensions)-navigation_surface_axis(a,view.dimensions),
                gx=goal.x-a.x,gy=navigation_surface_axis(goal,view.dimensions)-navigation_surface_axis(a,view.dimensions);
            double edge=fmax(fabs(ex),fabs(ey)),ray=fmax(fmax(fabs(sx),fabs(sy)),fmax(fabs(gx),fabs(gy)));
            if(!isfinite(edge)||!isfinite(ray)||edge==0)return fail("navigation motion: polygon arithmetic outside numerical range",polygon);
            if(ray==0)continue;
            // both endpoint sides use one scale so their clipping ratio retains its meaning.
            double first=sign*navigation_cross2(ex/edge,ey/edge,sx/ray,sy/ray),last=sign*navigation_cross2(ex/edge,ey/edge,gx/ray,gy/ray);
            if(!isfinite(first)||!isfinite(last))return fail("navigation motion: clipping outside numerical range",polygon);
            if(first<0&&last<0){*span=out;return true;}
            if(first<0||last<0) {
                double t=first/(first-last);if(!isfinite(t))return fail("navigation motion: invalid clipping parameter",polygon);
                if(first<0)out.low=fmax(out.low,t);else out.high=fmin(out.high,t);
                if(out.low>out.high){*span=out;return true;}
            }
        }
        out.hit=true;out.crossing=at(crossing,vertices[0],normal);out.end=at(out.high,vertices[0],normal);
        if(!navigation_vec_finite(out.crossing)||!navigation_vec_finite(out.end))return fail("navigation motion: grounded point outside numerical range",polygon);
        *span=out;return true;
    }
    bool next(uint64_t polygon,const Span& current,double t,uint64_t* next_polygon,Span* next_span) {
        Candidate fan[NAVIGATION_MOVE_MAX_POLYGONS];fan[0]={polygon,SIZE_MAX,current};size_t count=1;
        for(size_t cursor=0;cursor<count;cursor++) {
            size_t first=0,edges=0;
            if(!navigation_outgoing_edges(view.graph,fan[cursor].polygon,&first,&edges))return fail("navigation motion: missing polygon adjacency",fan[cursor].polygon);
            for(size_t i=0;i<edges;i++) {
                if(result.edge_tests==query.max_edge_tests){result.status=NAVIGATION_EXHAUSTED;return true;}result.edge_tests++;
                if(!navigation_edge_admitted(view.graph,first+i,&query.profile.path.search))continue;
                NavigationEdgeDesc edge={};navigation_edge_at(view.graph,first+i,&edge);
                bool seen=false;for(size_t j=0;j<count;j++)seen|=fan[j].polygon==edge.to;if(seen)continue;
                Span span;
                if(!clip(edge.to,t,&span)) {if(error&&error->message)return false;result.status=NAVIGATION_EXHAUSTED;return true;}
                // shared edges can produce adjacent clipping parameters a few arithmetic ulps apart.
                double tolerance=32*DBL_EPSILON;
                if(!span.hit||span.low>t+tolerance||span.high<t-tolerance)continue;
                if(count==NAVIGATION_MOVE_MAX_POLYGONS){result.status=NAVIGATION_EXHAUSTED;return true;}
                size_t added=count++;fan[added]={edge.to,cursor,span};
                if(span.high<=t)continue;
                size_t chain[NAVIGATION_MOVE_MAX_POLYGONS],length=0;
                for(size_t at=added;at;at=fan[at].parent)chain[length++]=at;
                for(size_t j=length;j;j--) {
                    const auto& entry=fan[chain[j-1]];
                    if(result.polygon_count==NAVIGATION_MOVE_MAX_POLYGONS){result.status=NAVIGATION_EXHAUSTED;return true;}
                    if(!append(entry.span.crossing))return false;result.polygons[result.polygon_count++]=entry.polygon;result.polygon=entry.polygon;result.end=entry.span.crossing;
                }
                *next_polygon=edge.to;*next_span=span;return true;
            }
        }
        result.status=NAVIGATION_PARTIAL;return true;
    }
};
}
bool navigation_surface_walk(const NavigationSurfaceView* view,const NavigationSurfaceMove* query,NavigationSurfaceMotion* output,NavigationError* error) {
    if(!navigation_surface_profile_valid(view,query?&query->profile:nullptr,error))return false;
    auto fail=[&](const char* message){if(error)*error={message,0};return false;};
    if(!output||!query->polygon||query->polygon>view->polygon_count||query->max_polygon_tests>NAVIGATION_MOVE_MAX_POLYGONS||query->max_edge_tests>NAVIGATION_MAX_EDGES||
        !navigation_vec_finite(query->profile.path.start)||!navigation_vec_finite(query->profile.path.goal)||
        (view->dimensions==2&&(query->profile.path.start.z!=0||query->profile.path.goal.z!=0))||
        !isfinite(query->profile.path.max_projection_distance)||query->profile.path.max_projection_distance<0)
        return fail("navigation motion: invalid endpoints, polygon or work budget");
    NavigationSurfaceMotion result={};result.status=NAVIGATION_UNREACHABLE;result.start=result.end=query->profile.path.start;
    if(!navigation_node_admitted(view->graph,query->polygon,&query->profile.path.search)){*output=result;return true;}
    if(!query->max_polygon_tests){result.status=NAVIGATION_EXHAUSTED;*output=result;return true;}
    if(!view->closest(view->owner,query->polygon,result.start,&result.start)||!navigation_vec_finite(result.start))return fail("navigation motion: start projection outside numerical range");
    result.start_distance=navigation_vec_distance(query->profile.path.start,result.start);
    if(!isfinite(result.start_distance))return fail("navigation motion: start distance outside numerical range");
    if(result.start_distance>query->profile.path.max_projection_distance){*output=result;return true;}
    result.end=result.start;NavigationError failed={};MotionBuild build(*view,*query,result,&failed);Span span;
    if(!build.clip(query->polygon,0,&span)){if(error)*error=failed;return false;}
    if(!span.hit||span.low>32*DBL_EPSILON){*output=result;return true;}
    if(!build.append(result.start)){if(error)*error=failed;return false;}
    uint64_t polygon=query->polygon;result.polygon=polygon;result.polygons[result.polygon_count++]=polygon;result.status=NAVIGATION_COMPLETE;
    for(;;) {
        if(!build.append(span.end)){if(error)*error=failed;return false;}
        result.end=span.end;result.fraction=span.high;
        if(span.high>=1)break;
        uint64_t next=0;Span following;
        if(!build.next(polygon,span,span.high,&next,&following)){if(error)*error=failed;return false;}
        if(!next)break;polygon=next;span=following;
    }
    build.finish();*output=result;return true;
}
