#include "navigation.h"
#include "arraylist.hpp"
#include "mem.h"
#include "digest.h"
#include "hex.h"
#include <float.h>

struct BakePortal {uint64_t from,to;size_t boundary,low,high;bool x_axis,positive;};
struct NavigationBake {
    unsigned references=1;
    NavigationBakeSettings settings={};NavigationBakeStats stats={};NavigationGraph* graph=nullptr;
    lam::ArrayList<NavigationBakeCell> cells;
    lam::ArrayList<NavigationBakeRegion> regions;
    lam::ArrayList<BakePortal> portals;
    ~NavigationBake(){navigation_destroy(graph);}
};
namespace {
constexpr size_t fragments_per_column=64,polygon_vertices=12,coverage_events=4096;
struct Point {double x,y,z;}; // y is measured in vertical voxel units
struct Fragment {
    size_t column;Point polygon[polygon_vertices];unsigned count;
    double low,high,cost;uint32_t groups;bool walkable,area;
};
struct Candidate {NavigationBakeCell cell;size_t column;uint64_t node=0;bool clear=false;uint64_t region=0;};
struct Column {size_t begin=0,end=0;};
struct Edge {Point a,b;};
struct Interval {long double low,high;};
static NavigationBakeRegion cell_region(const NavigationBakeCell& cell) {
    return {cell.x,cell.z,1,1,cell.floor,cell.ceiling,cell.cost,cell.groups};
}
static NavigationBakeRegion domain_region(const NavigationBake* bake,size_t index) {
    return bake->settings.representation==NAVIGATION_BAKE_POLYGONS?bake->regions[index]:cell_region(bake->cells[index]);
}
static size_t domain_count(const NavigationBake* bake) {
    return bake->settings.representation==NAVIGATION_BAKE_POLYGONS?bake->regions.size():bake->cells.size();
}
static void region_bounds(const NavigationBake* bake,const NavigationBakeRegion& region,NavigationVec* low,NavigationVec* high) {
    const auto& s=bake->settings;double x=s.low.x+region.x*s.cell_size,z=s.low.z+region.z*s.cell_size;
    *low={x,region.floor,z};*high={x+fmin(region.width*s.cell_size,s.high.x-x),region.floor,z+fmin(region.depth*s.cell_size,s.high.z-z)};
}
struct Build {
    const NavigationBakeDesc& desc;NavigationError* error;NavigationBake& result;
    NavigationBakeStage stage=NAVIGATION_BAKE_RASTER;size_t notified=0;
    bool fail(const char* message,size_t id=0){if(error)*error={message,id};return false;}
    bool work(size_t count=1) {
        if(count>desc.settings.max_work-result.stats.work)return fail("navigation bake: work budget exhausted");
        result.stats.work+=count;
        if(desc.proceed&&(!notified||result.stats.work-notified>=1024)) {
            notified=result.stats.work;
            if(!desc.proceed(desc.owner,stage,result.stats.work))return fail("navigation bake: cancelled");
        }
        return true;
    }
    bool phase(NavigationBakeStage next){stage=next;notified=0;return work(0);}
};
static int fragment_order(const void* a,const void* b) {
    const auto &x=*(const Fragment*)a,&y=*(const Fragment*)b;
    if(x.column!=y.column)return x.column>y.column?1:-1;
    if(x.low!=y.low)return x.low>y.low?1:-1;
    if(x.high!=y.high)return x.high>y.high?1:-1;
    return 0;
}
static int number_order(const void* a,const void* b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static int interval_order(const void* a,const void* b) {
    auto &x=*(const Interval*)a,&y=*(const Interval*)b;
    return x.low!=y.low?(x.low>y.low?1:-1):(x.high>y.high)-(x.high<y.high);
}
static double axis(Point p,unsigned index){return index==0?p.x:index==1?p.y:p.z;}
static Point crossing(Point a,Point b,unsigned index,double boundary) {
    // canonical endpoint order makes shared clipped edges independent of winding.
    if(axis(a,index)>axis(b,index)){Point temporary=a;a=b;b=temporary;}
    double scale=fmax(fabs(boundary),fmax(fabs(axis(a,index)),fabs(axis(b,index)))),
        t=(boundary/scale-axis(a,index)/scale)/(axis(b,index)/scale-axis(a,index)/scale);
    Point p={a.x*(1-t)+b.x*t,a.y*(1-t)+b.y*t,a.z*(1-t)+b.z*t};
    if(index==0)p.x=boundary;else if(index==1)p.y=boundary;else p.z=boundary;return p;
}
static unsigned clip(const Point* input,unsigned count,Point* output,unsigned index,double boundary,bool above) {
    unsigned size=0;if(!count)return 0;
    for(unsigned i=0;i<count;i++) {
        Point a=input[i],b=input[(i+1)%count];bool in_a=above?axis(a,index)>=boundary:axis(a,index)<=boundary,
            in_b=above?axis(b,index)>=boundary:axis(b,index)<=boundary;
        if(in_a)output[size++]=a;
        if(in_a!=in_b)output[size++]=crossing(a,b,index,boundary);
    }
    return size;
}
static bool raster(Build& build,size_t nx,size_t nz,lam::ArrayList<Fragment>& fragments) {
    const auto& settings=build.desc.settings;double cosine=cos(settings.max_slope);
    for(size_t i=0;i<build.desc.triangle_count;i++) {
        if(!build.work())return false;const auto& triangle=build.desc.triangles[i];double normal[3];
        if(!navigation_vec_finite(triangle.vertices[0])||!navigation_vec_finite(triangle.vertices[1])||!navigation_vec_finite(triangle.vertices[2])||
            !isfinite(triangle.cost)||triangle.cost<0||!navigation_triangle_normal(triangle.vertices[0],triangle.vertices[1],triangle.vertices[2],normal))
            return build.fail("navigation bake: invalid source triangle",i+1);
        Point vertices[3];double xmin=INFINITY,xmax=-INFINITY,zmin=INFINITY,zmax=-INFINITY;
        for(unsigned j=0;j<3;j++) {
            auto p=triangle.vertices[j];vertices[j]={(p.x-settings.low.x)/settings.cell_size,(p.y-settings.low.y)/settings.cell_height,(p.z-settings.low.z)/settings.cell_size};
            Point v=vertices[j];if(!isfinite(v.x)||!isfinite(v.y)||!isfinite(v.z))return build.fail("navigation bake: source outside numerical range",i+1);
            xmin=fmin(xmin,v.x);xmax=fmax(xmax,v.x);zmin=fmin(zmin,v.z);zmax=fmax(zmax,v.z);
        }
        if(xmax<0||zmax<0||xmin>nx||zmin>nz)continue;
        size_t x0=(size_t)fmax(0,fmin(nx-1,ceil(xmin)-1)),x1=(size_t)fmax(0,fmin(nx-1,floor(xmax))),
            z0=(size_t)fmax(0,fmin(nz-1,ceil(zmin)-1)),z1=(size_t)fmax(0,fmin(nz-1,floor(zmax)));
        for(size_t z=z0;z<=z1;z++)for(size_t x=x0;x<=x1;x++) {
            if(!build.work())return false;Point buffers[2][polygon_vertices];unsigned count=3,side=0;
            for(unsigned j=0;j<3;j++)buffers[0][j]={vertices[j].x-x,vertices[j].y,vertices[j].z-z};
            double width=fmin(1,(settings.high.x-settings.low.x)/settings.cell_size-x),depth=fmin(1,(settings.high.z-settings.low.z)/settings.cell_size-z),
                top=(settings.high.y-settings.low.y)/settings.cell_height;
            struct Plane {unsigned axis;double boundary;bool above;};
            Plane planes[]={{0,0,true},{0,width,false},{2,0,true},{2,depth,false},{1,0,true},{1,top,false}};
            for(auto plane:planes){count=clip(buffers[side],count,buffers[1-side],plane.axis,plane.boundary,plane.above);side=1-side;if(!count)break;}
            if(!count)continue;
            Fragment fragment={};fragment.column=x+nx*z;fragment.count=count;fragment.low=INFINITY;fragment.high=-INFINITY;
            fragment.walkable=triangle.walkable&&normal[1]>=cosine;fragment.cost=triangle.cost;fragment.groups=triangle.groups;long double area=0;
            for(unsigned j=0;j<count;j++) {
                Point a=buffers[side][j],b=buffers[side][(j+1)%count];fragment.polygon[j]=a;
                if(!isfinite(a.x)||!isfinite(a.y)||!isfinite(a.z))return build.fail("navigation bake: clipped source outside numerical range",i+1);
                fragment.low=fmin(fragment.low,a.y);fragment.high=fmax(fragment.high,a.y);area+=(long double)a.x*b.z-(long double)b.x*a.z;
            }
            fragment.low=floor(fragment.low);fragment.high=ceil(fragment.high);fragment.area=area!=0;
            // floor edges on a neighboring cell's boundary are not ceilings in that cell.
            if(!fragment.area&&normal[1]!=0)continue;
            if(fragments.size()==settings.max_fragments)return build.fail("navigation bake: fragment quota exceeded",i+1);
            if(!fragments.append(fragment))return build.fail("navigation bake: fragment allocation failed");
        }
    }
    qsort(fragments.data(),fragments.size(),sizeof(Fragment),fragment_order);build.result.stats.fragments=fragments.size();return true;
}
static long double edge_height(Edge edge,double x) {
    if(edge.a.x>edge.b.x){Point t=edge.a;edge.a=edge.b;edge.b=t;}
    return (long double)edge.a.z+((long double)edge.b.z-edge.a.z)*((long double)x-edge.a.x)/((long double)edge.b.x-edge.a.x);
}
static bool coverage(Build& build,const Fragment* fragments,size_t count,double width,double depth,bool* covered) {
    *covered=false;lam::ArrayList<Edge> edges;lam::ArrayList<double> events;Interval intervals[fragments_per_column];
    if(!events.append(0)||!events.append(width))return build.fail("navigation bake: coverage allocation failed");
    for(size_t i=0;i<count;i++)if(fragments[i].walkable&&fragments[i].area)for(unsigned j=0;j<fragments[i].count;j++) {
        Point a=fragments[i].polygon[j],b=fragments[i].polygon[(j+1)%fragments[i].count];
        if(!edges.append({a,b})||!events.append(a.x))return build.fail("navigation bake: coverage allocation failed");
    }
    // edge intersections partition the sweep into strips with fixed interval order.
    // Testing only corners/centres or summing triangle areas would miss tiny holes or overlaps.
    for(size_t i=0;i<edges.size();i++)for(size_t j=0;j<i;j++) {
        if(!build.work())return false;auto a=edges[i],b=edges[j];
        long double ax=(long double)a.b.x-a.a.x,az=(long double)a.b.z-a.a.z,bx=(long double)b.b.x-b.a.x,bz=(long double)b.b.z-b.a.z,
            dx=(long double)b.a.x-a.a.x,dz=(long double)b.a.z-a.a.z,denominator=ax*bz-az*bx;
        if(denominator==0)continue;long double t=(dx*bz-dz*bx)/denominator,u=(dx*az-dz*ax)/denominator;
        if(t<=0||t>=1||u<=0||u>=1)continue;double x=(double)((long double)a.a.x+ax*t);
        if(events.size()==coverage_events)return build.fail("navigation bake: coverage event quota exceeded");
        if(!events.append(x))return build.fail("navigation bake: coverage allocation failed");
    }
    qsort(events.data(),events.size(),sizeof(double),number_order);
    for(size_t event=1;event<events.size();event++) {
        double left=events[event-1],right=events[event];if(left==right)continue;
        // both limiting sections avoid rounding a tiny strip's midpoint onto an event.
        for(unsigned section=0;section<2;section++) {
        double x=section?right:left;size_t size=0;
        for(size_t i=0;i<count;i++)if(fragments[i].walkable&&fragments[i].area) {
            if(!build.work(fragments[i].count))return false;long double low=INFINITY,high=-INFINITY;
            for(unsigned j=0;j<fragments[i].count;j++) {
                Edge edge={fragments[i].polygon[j],fragments[i].polygon[(j+1)%fragments[i].count]};
                if(edge.a.x==edge.b.x||left<fmin(edge.a.x,edge.b.x)||right>fmax(edge.a.x,edge.b.x))continue;
                long double z=edge_height(edge,x);if(z<low)low=z;if(z>high)high=z;
            }
            if(low<=high)intervals[size++]={low,high};
        }
        qsort(intervals,size,sizeof(Interval),interval_order);long double cursor=0;
        for(size_t i=0;i<size;i++){if(intervals[i].low>cursor)return true;if(intervals[i].high>cursor)cursor=intervals[i].high;}
        if(cursor<depth)return true;
        }
    }
    *covered=true;return true;
}
static bool candidates(Build& build,size_t nx,const lam::ArrayList<Fragment>& fragments,
        lam::ArrayList<Candidate>& cells,lam::ArrayList<Column>& columns) {
    const auto& s=build.desc.settings;double maximum_rise=sqrt(2.0)*s.cell_size*tan(s.max_slope)+2*s.cell_height;
    for(size_t start=0;start<fragments.size();) {
        size_t end=start+1,column=fragments[start].column;while(end<fragments.size()&&fragments[end].column==column)end++;
        if(end-start>fragments_per_column)return build.fail("navigation bake: per-column fragment quota exceeded",column+1);
        columns[column].begin=cells.size();
        for(size_t at=start;at<end;) {
            double low=fragments[at].low,high=fragments[at].high;size_t next=at+1;
            while(next<end&&fragments[next].low<=high){high=fmax(high,fragments[next].high);next++;}
            double floor=s.low.y+high*s.cell_height,ceiling=next<end?s.low.y+fragments[next].low*s.cell_height:s.high.y;
            bool admitted=ceiling-floor>=s.height&&(high-low)*s.cell_height<=maximum_rise;uint32_t groups=UINT32_MAX;double cost=0,walkable_top=-INFINITY;
            for(size_t i=at;i<next;i++) {
                if(!build.work())return false;const auto& f=fragments[i];
                if(f.area&&!f.walkable)admitted=false;
                if(f.area&&f.walkable){groups&=f.groups;cost=fmax(cost,f.cost);walkable_top=fmax(walkable_top,f.high);}
            }
            admitted=admitted&&(high-walkable_top)*s.cell_height<=s.max_step;
            if(admitted&&groups) {
                bool covered;size_t x=column%nx,z=column/nx;
                double width=fmin(1,(s.high.x-s.low.x)/s.cell_size-x),depth=fmin(1,(s.high.z-s.low.z)/s.cell_size-z);
                if(!coverage(build,fragments.data()+at,next-at,width,depth,&covered))return false;
                if(covered) {
                    if(cells.size()==s.max_cells)return build.fail("navigation bake: candidate cell quota exceeded");
                    if(!cells.append({{x,z,floor,ceiling,cost,groups},column,0,false}))return build.fail("navigation bake: candidate allocation failed");
                }
            }
            at=next;
        }
        columns[column].end=cells.size();start=end;
    }
    build.result.stats.candidates=cells.size();return true;
}
static bool compatible(const NavigationBakeCell& a,const NavigationBakeCell& b,const NavigationBakeSettings& s) {
    return fabs(a.floor-b.floor)<=s.max_step&&fmin(a.ceiling,b.ceiling)-fmax(a.floor,b.floor)>=s.height;
}
static Candidate* match(lam::ArrayList<Candidate>& cells,Column column,const NavigationBakeCell& source,const NavigationBakeSettings& s,bool clear) {
    for(size_t i=column.begin;i<column.end;i++)if((!clear||cells[i].clear)&&compatible(source,cells[i].cell,s))return &cells[i];return nullptr;
}
static bool erode(Build& build,size_t nx,size_t nz,lam::ArrayList<Candidate>& candidates,const lam::ArrayList<Column>& columns) {
    const auto& s=build.desc.settings;
    for(auto& candidate:candidates) {
        if(!build.work())return false;auto cell=candidate.cell;double x=cell.x*s.cell_size,z=cell.z*s.cell_size,
            width=fmin(s.cell_size,s.high.x-s.low.x-x),depth=fmin(s.cell_size,s.high.z-s.low.z-z);
        if(x<s.radius||z<s.radius||s.high.x-s.low.x-x-width<s.radius||s.high.z-s.low.z-z-depth<s.radius)continue;
        bool clear=true;uint32_t groups=cell.groups;double cost=cell.cost;
        // rectangle distance erodes the whole published cell, not just its centre.
        size_t radius=s.radius>0?(size_t)ceil(s.radius/s.cell_size)+1:0;
        for(int64_t dz=-(int64_t)radius;dz<=(int64_t)radius&&clear;dz++)for(int64_t dx=-(int64_t)radius;dx<=(int64_t)radius;dx++) {
            if(!build.work())return false;double sx=fmax(fabs((double)dx)-1,0)*s.cell_size,sz=fmax(fabs((double)dz)-1,0)*s.cell_size;
            if((dx||dz)&&hypot(sx,sz)>=s.radius)continue;
            int64_t cx=(int64_t)cell.x+dx,cz=(int64_t)cell.z+dz;
            if(cx<0||cz<0||(size_t)cx>=nx||(size_t)cz>=nz){clear=false;break;}
            Candidate* neighbor=match(candidates,columns[(size_t)cx+nx*(size_t)cz],cell,s,false);
            if(!neighbor){clear=false;break;}groups&=neighbor->cell.groups;cost=fmax(cost,neighbor->cell.cost);
        }
        if(clear&&groups){candidate.clear=true;candidate.node=build.result.cells.size()+1;cell.groups=groups;cell.cost=cost;
            // erosion reads the original domain; revised masks must not propagate by iteration order.
            if(!build.result.cells.append(cell))return build.fail("navigation bake: result cell allocation failed");}
    }
    return true;
}
// equal floor/ceiling and area attributes make each merged rectangle one convex domain.
static Candidate* region_cell(Build& build,lam::ArrayList<Candidate>& cells,const lam::ArrayList<Column>& columns,
        size_t column,const NavigationBakeCell& source,bool* valid) {
    auto range=columns[column];
    for(size_t i=range.begin;i<range.end;i++) {
        if(!build.work()){*valid=false;return nullptr;}auto& candidate=cells[i];
        if(!candidate.clear||candidate.region)continue;auto cell=build.result.cells[candidate.node-1];
        if(cell.floor==source.floor&&cell.ceiling==source.ceiling&&cell.cost==source.cost&&cell.groups==source.groups)return &candidate;
    }
    return nullptr;
}
static bool partition(Build& build,size_t nx,size_t nz,lam::ArrayList<Candidate>& cells,const lam::ArrayList<Column>& columns) {
    bool valid=true;
    for(auto& candidate:cells)if(candidate.clear&&!candidate.region) {
        auto source=build.result.cells[candidate.node-1];size_t width=1,depth=1;
        auto at=[&](size_t x,size_t z){return region_cell(build,cells,columns,x+nx*z,source,&valid);};
        while(source.x+width<nx&&at(source.x+width,source.z))width++;
        if(!valid)return false;
        while(source.z+depth<nz) {
            bool full=true;for(size_t x=0;x<width&&full;x++)full=at(source.x+x,source.z+depth)!=nullptr;
            if(!valid)return false;if(!full)break;depth++;
        }
        NavigationBakeRegion region=cell_region(source);region.width=width;region.depth=depth;
        if(!build.result.regions.append(region))return build.fail("navigation bake: region allocation failed");
        for(size_t z=0;z<depth;z++)for(size_t x=0;x<width;x++) {
            auto* member=at(source.x+x,source.z+z);if(!valid)return false;
            if(!member)return build.fail("navigation bake: partition lost a region cell");member->region=build.result.regions.size();
        }
    }
    return true;
}
static int portal_order(const void* a,const void* b) {
    const auto &x=*(const BakePortal*)a,&y=*(const BakePortal*)b;
    if(x.from!=y.from)return x.from>y.from?1:-1;if(x.to!=y.to)return x.to>y.to?1:-1;
    if(x.x_axis!=y.x_axis)return x.x_axis?1:-1;if(x.boundary!=y.boundary)return x.boundary>y.boundary?1:-1;
    if(x.positive!=y.positive)return x.positive?1:-1;
    if(x.low!=y.low)return x.low>y.low?1:-1;return (x.high>y.high)-(x.high<y.high);
}
static bool graph(Build& build,size_t nx,size_t nz,lam::ArrayList<Candidate>& candidates,const lam::ArrayList<Column>& columns) {
    const auto& s=build.desc.settings;bool polygons=s.representation==NAVIGATION_BAKE_POLYGONS;
    if(polygons&&!partition(build,nx,nz,candidates,columns))return false;
    lam::ArrayList<NavigationNodeDesc> nodes;lam::ArrayList<NavigationEdgeDesc> edges;lam::ArrayList<BakePortal> portals;
    for(size_t i=0;i<domain_count(&build.result);i++) {
        if(!build.work())return false;auto region=domain_region(&build.result,i);NavigationVec low,high;
        region_bounds(&build.result,region,&low,&high);NavigationVec p={low.x+(high.x-low.x)*.5,region.floor,low.z+(high.z-low.z)*.5};
        if(!navigation_vec_finite(p))return build.fail("navigation bake: domain centre outside numerical range");
        if(!nodes.append({i+1,p,region.groups,false}))return build.fail("navigation bake: graph node allocation failed");
    }
    for(auto& candidate:candidates)if(candidate.clear) {
        if(!build.work())return false;auto cell=build.result.cells[candidate.node-1];
        const int offsets[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
        for(auto& offset:offsets) {
            int64_t cx=(int64_t)cell.x+offset[0],cz=(int64_t)cell.z+offset[1];
            if(cx<0||cz<0||(size_t)cx>=nx||(size_t)cz>=nz)continue;
            auto* neighbor=match(candidates,columns[(size_t)cx+nx*(size_t)cz],cell,s,true);if(!neighbor)continue;
            if(polygons) {
                if(candidate.region==neighbor->region)continue;
                bool x_axis=offset[0]!=0,positive=x_axis?offset[0]>0:offset[1]>0;
                size_t boundary=x_axis?cell.x+(positive?1:0):cell.z+(positive?1:0),low=x_axis?cell.z:cell.x;
                if(!portals.append({candidate.region,neighbor->region,boundary,low,low+1,x_axis,positive}))return build.fail("navigation bake: portal allocation failed");
            }else {
                double cost=hypot(s.cell_size,cell.floor-neighbor->cell.floor)*build.result.cells[neighbor->node-1].cost;
                if(!isfinite(cost))return build.fail("navigation bake: edge cost outside numerical range");
                NavigationEdgeDesc edge={};edge.from=candidate.node;edge.to=neighbor->node;edge.cost=cost;edge.membership=UINT32_MAX;
                if(!edges.append(edge))return build.fail("navigation bake: graph edge allocation failed");
            }
        }
    }
    if(polygons) {
        if(portals.size()>1)qsort(portals.data(),portals.size(),sizeof(BakePortal),portal_order);
        for(auto portal:portals) {
            if(!build.work())return false;
            if(!build.result.portals.empty()) {
                auto& previous=build.result.portals.back();
                if(previous.from==portal.from&&previous.to==portal.to) {
                    if(previous.x_axis!=portal.x_axis||previous.positive!=portal.positive||previous.boundary!=portal.boundary||previous.high<portal.low)
                        return build.fail("navigation bake: disconnected convex-region portal");
                    previous.high=portal.high;continue;
                }
            }
            if(!build.result.portals.append(portal))return build.fail("navigation bake: merged portal allocation failed");
        }
        for(auto& portal:build.result.portals) {
            double distance=navigation_vec_distance(nodes[portal.from-1].position,nodes[portal.to-1].position),cost=distance*build.result.regions[portal.to-1].cost;
            if(!isfinite(cost))return build.fail("navigation bake: region edge cost outside numerical range");
            NavigationEdgeDesc edge={};edge.from=portal.from;edge.to=portal.to;edge.cost=cost;edge.membership=UINT32_MAX;
            if(!edges.append(edge))return build.fail("navigation bake: region graph allocation failed");
        }
    }
    build.result.stats.cells=build.result.cells.size();build.result.stats.edges=edges.size();if(nodes.empty())return true;
    NavigationDesc desc={3,nodes.data(),nodes.size(),edges.data(),edges.size()};return navigation_create(&desc,&build.result.graph,build.error);
}
}
NavigationBake* navigation_bake_retain(NavigationBake* bake){if(bake)__atomic_add_fetch(&bake->references,1,__ATOMIC_RELAXED);return bake;}
void navigation_bake_destroy(NavigationBake* bake){if(bake&&__atomic_sub_fetch(&bake->references,1,__ATOMIC_ACQ_REL)==0){bake->~NavigationBake();mem_free(bake);}}
const NavigationGraph* navigation_bake_graph(const NavigationBake* bake){return bake?bake->graph:nullptr;}
bool navigation_bake_cell(const NavigationBake* bake,uint64_t id,NavigationBakeCell* cell) {
    if(!bake||!cell||!id||id>bake->cells.size())return false;*cell=bake->cells[(size_t)id-1];return true;
}
size_t navigation_bake_region_count(const NavigationBake* bake){return bake?bake->regions.size():0;}
bool navigation_bake_region(const NavigationBake* bake,uint64_t id,NavigationBakeRegion* region) {
    if(!bake||!region||!id||id>bake->regions.size())return false;*region=bake->regions[(size_t)id-1];return true;
}
bool navigation_bake_stats(const NavigationBake* bake,NavigationBakeStats* stats){if(!bake||!stats)return false;*stats=bake->stats;return true;}
bool navigation_bake_settings(const NavigationBake* bake,NavigationBakeSettings* settings){if(!bake||!settings)return false;*settings=bake->settings;return true;}
bool navigation_bake_settings_hash(const NavigationBakeSettings* settings,char output[65]) {
    if(!settings||!output)return false;const auto& s=*settings;DigestCtx* digest=digest_ctx_new(256);if(!digest)return false;
    double numbers[]={s.low.x,s.low.y,s.low.z,s.high.x,s.high.y,s.high.z,s.cell_size,s.cell_height,s.radius,s.height,s.max_step,s.max_slope};
    bool valid=digest_update_u64_le(digest,1);
    // JSON preserves numeric zero but discards its sign; settings fingerprints use that same value identity.
    for(double number:numbers){if(number==0)number=0;valid=valid&&digest_update_f64_le(digest,number);}
    valid=valid&&digest_update_u64_le(digest,s.max_cells)&&digest_update_u64_le(digest,s.max_fragments)&&digest_update_u64_le(digest,s.max_work);
    // existing cell archives keep their fingerprint; polygon connectivity is a distinct profile.
    if(s.representation!=NAVIGATION_BAKE_CELLS)valid=valid&&digest_update_u64_le(digest,2)&&digest_update_u64_le(digest,s.representation);
    uint8_t bytes[32];valid=valid&&digest_finalize(digest,bytes,sizeof(bytes));digest_ctx_free(digest);if(valid)hex_encode(bytes,sizeof(bytes),output);return valid;
}
bool navigation_bake_equal(const NavigationBake* a,const NavigationBake* b) {
    if(a==b)return true;if(!a||!b||a->cells.size()!=b->cells.size()||!navigation_equal(a->graph,b->graph))return false;
    const auto &x=a->settings,&y=b->settings;
    if(x.low.x!=y.low.x||x.low.y!=y.low.y||x.low.z!=y.low.z||x.high.x!=y.high.x||x.high.y!=y.high.y||x.high.z!=y.high.z||
        x.cell_size!=y.cell_size||x.cell_height!=y.cell_height||x.radius!=y.radius||x.height!=y.height||x.max_step!=y.max_step||x.max_slope!=y.max_slope||
        x.max_cells!=y.max_cells||x.max_fragments!=y.max_fragments||x.max_work!=y.max_work||x.representation!=y.representation)return false;
    for(size_t i=0;i<a->cells.size();i++){const auto &c=a->cells[i],&d=b->cells[i];
        if(c.x!=d.x||c.z!=d.z||c.floor!=d.floor||c.ceiling!=d.ceiling||c.cost!=d.cost||c.groups!=d.groups)return false;}
    return true;
}
static NavigationVec bake_closest(const NavigationBake* bake,uint64_t id,NavigationVec p) {
    auto region=domain_region(bake,(size_t)id-1);NavigationVec low,high;region_bounds(bake,region,&low,&high);
    return {fmax(low.x,fmin(p.x,high.x)),region.floor,fmax(low.z,fmin(p.z,high.z))};
}
static const BakePortal* path_portal(const NavigationBake* bake,uint64_t from,uint64_t to,
        const NavigationMeshQuery& query,NavigationMeshPath& path,NavigationError* error) {
    size_t low=0,high=bake->portals.size();
    while(low<high) {
        if(!navigation_surface_work(&query,&path))return nullptr;size_t middle=low+(high-low)/2;auto& portal=bake->portals[middle];
        if(portal.from==from&&portal.to==to)return &portal;
        if(portal.from<from||(portal.from==from&&portal.to<to))low=middle+1;else high=middle;
    }
    if(error)*error={"navigation bake: region corridor has no portal",from};return nullptr;
}
static NavigationPortal world_portal(const NavigationBake* bake,const BakePortal& portal,double floor) {
    NavigationBakeRegion strip={portal.x_axis?portal.boundary:portal.low,portal.x_axis?portal.low:portal.boundary,
        portal.x_axis?0:portal.high-portal.low,portal.x_axis?portal.high-portal.low:0,floor,0,0,0};
    NavigationVec low,high;region_bounds(bake,strip,&low,&high);
    return portal.positive==portal.x_axis?NavigationPortal{high,low}:NavigationPortal{low,high};
}
static bool region_assemble(const NavigationBake* bake,const NavigationMeshQuery& query,NavigationMeshPath& path,NavigationError* error) {
    size_t count=path.corridor.count;lam::ArrayList<double> costs;lam::ArrayList<NavigationPortal> arrivals;path.portal_count=count-1;
    if(count>1)path.portals=(NavigationPortal*)mem_alloc((count-1)*sizeof(NavigationPortal),MEM_CAT_EVAL);
    auto fail=[&](){if(error)*error={"navigation bake: region path allocation failed",0};return false;};
    if(count>1&&!path.portals)return fail();
    for(size_t i=0;i<count;i++) {
        auto& region=bake->regions[path.corridor.nodes[i]-1];if(!costs.append(region.cost))return fail();
        if(i+1==count)continue;
        auto* portal=path_portal(bake,path.corridor.nodes[i],path.corridor.nodes[i+1],query,path,error);if(!portal)return false;
        path.portals[i]=world_portal(bake,*portal,region.floor);
        if(!arrivals.append(world_portal(bake,*portal,bake->regions[portal->to-1].floor)))return fail();
    }
    return navigation_surface_assemble(3,&query,&path,costs.data(),arrivals.data(),error);
}
static bool bake_assemble(const void* owner,const NavigationMeshQuery* query,NavigationMeshPath* output,NavigationError* error) {
    const auto* bake=(const NavigationBake*)owner;auto& path=*output;const auto& q=*query;size_t count=path.corridor.count;
    auto fail=[&](const char* message){if(error)*error={message,0};return false;};
    if(bake->settings.representation==NAVIGATION_BAKE_POLYGONS)return region_assemble(bake,q,path,error);
    path.points=(NavigationVec*)mem_alloc((count+2)*sizeof(NavigationVec),MEM_CAT_EVAL);
    if(!path.points)return fail("navigation bake: waypoint allocation failed");
    bool valid=true;
    auto append=[&](NavigationVec p,uint64_t area){
        if(!navigation_surface_work(&q,&path))return false;
        return navigation_waypoint_append(&path,p,bake->cells[(size_t)area-1].cost,error);
    };
    valid=append(path.projected_start,path.start_polygon);
    if(count>1)for(size_t i=0;i<count&&valid;i++) {
        NavigationNodeDesc node={};navigation_node(bake->graph,path.corridor.nodes[i],&node);valid=append(node.position,node.id);
    }
    if(valid)valid=append(path.path_end,path.corridor.nodes[count-1]);
    return valid;
}
static void* bake_retain_owner(void* owner){return navigation_bake_retain((NavigationBake*)owner);}
static void bake_release_owner(void* owner){navigation_bake_destroy((NavigationBake*)owner);}
static bool bake_view_closest(const void* owner,uint64_t polygon,NavigationVec point,NavigationVec* result) {
    *result=bake_closest((const NavigationBake*)owner,polygon,point);return navigation_vec_finite(*result);
}
static double bake_view_cost(const void* owner,uint64_t polygon){return domain_region((const NavigationBake*)owner,(size_t)polygon-1).cost;}
static bool bake_view_equal(const void* left,const void* right){return navigation_bake_equal((const NavigationBake*)left,(const NavigationBake*)right);}
static bool bake_view_polygon(const void* owner,uint64_t polygon,NavigationVec* vertices,size_t capacity,size_t* count) {
    const auto* bake=(const NavigationBake*)owner;if(!polygon||polygon>domain_count(bake)||!vertices||capacity<4||!count)return false;
    NavigationVec low,high;region_bounds(bake,domain_region(bake,(size_t)polygon-1),&low,&high);
    vertices[0]=low;vertices[1]={high.x,low.y,low.z};vertices[2]=high;vertices[3]={low.x,low.y,high.z};*count=4;return true;
}
bool navigation_bake_surface(NavigationBake* bake,NavigationSurfaceView* view) {
    if(!bake||!view)return false;const auto& s=bake->settings;
    *view={3,domain_count(bake),bake->graph,bake,true,s.radius,s.height,s.max_step,
        bake_retain_owner,bake_release_owner,bake_view_closest,bake_view_cost,bake_assemble,bake_view_equal,bake_view_polygon};return true;
}
bool navigation_bake_find(const NavigationBake* bake,const NavigationBakeQuery* query,NavigationMeshPath* result,NavigationError* error) {
    NavigationSurfaceView view={};if(!navigation_bake_surface(const_cast<NavigationBake*>(bake),&view)) {
        if(error)*error={"navigation bake: missing domain",0};return false;
    }
    return navigation_surface_find(&view,query,result,error);
}
bool navigation_bake_settings_valid(const NavigationBakeSettings* settings,size_t* columns_x,size_t* columns_z,NavigationError* error) {
    auto fail=[&](const char* message){if(error)*error={message,0};return false;};
    if(!settings)return fail("navigation bake: missing settings");const auto& s=*settings;
    if((s.representation!=NAVIGATION_BAKE_CELLS&&s.representation!=NAVIGATION_BAKE_POLYGONS)||!navigation_vec_finite(s.low)||!navigation_vec_finite(s.high)||s.high.x<=s.low.x||s.high.y<=s.low.y||s.high.z<=s.low.z||
        !isfinite(s.cell_size)||s.cell_size<=0||!isfinite(s.cell_height)||s.cell_height<=0||
        !isfinite(s.radius)||s.radius<0||!isfinite(s.height)||s.height<=0||!isfinite(s.max_step)||s.max_step<0||s.max_step>=s.height||
        !isfinite(s.max_slope)||s.max_slope<0||s.max_slope>=acos(0)||!s.max_cells||s.max_cells>NAVIGATION_MAX_NODES||
        !s.max_fragments||s.max_fragments>NAVIGATION_BAKE_MAX_FRAGMENTS||!s.max_work||s.max_work>NAVIGATION_BAKE_MAX_WORK||
        s.low.x+s.cell_size<=s.low.x||s.low.z+s.cell_size<=s.low.z||s.low.y+s.cell_height<=s.low.y)
        return fail("navigation bake: invalid settings or quota");
    double x=ceil((s.high.x-s.low.x)/s.cell_size),z=ceil((s.high.z-s.low.z)/s.cell_size),y=(s.high.y-s.low.y)/s.cell_height;
    if(!isfinite(x)||!isfinite(z)||!isfinite(y)||x<1||z<1||y<=0||x>s.max_cells||z>s.max_cells||x*z>s.max_cells||y>1000000000)
        return fail("navigation bake: raster bounds exceed cell or numerical quota");
    if(columns_x)*columns_x=(size_t)x;if(columns_z)*columns_z=(size_t)z;return true;
}
bool navigation_bake_create(const NavigationBakeDesc* desc,NavigationBake** result,NavigationError* error) {
    if(result)*result=nullptr;
    auto fail=[&](const char* message){if(error)*error={message,0};return false;};
    if(!desc||!result||(!desc->triangles&&desc->triangle_count)||desc->triangle_count>NAVIGATION_BAKE_MAX_TRIANGLES)return fail("navigation bake: invalid input geometry or quota");
    size_t x,z;if(!navigation_bake_settings_valid(&desc->settings,&x,&z,error))return false;const auto& s=desc->settings;
    void* memory=mem_alloc(sizeof(NavigationBake),MEM_CAT_EVAL);if(!memory)return fail("navigation bake: result allocation failed");
    auto* bake=new(memory) NavigationBake();bake->settings=s;bake->stats.columns=x*z;Build build={*desc,error,*bake};
    lam::ArrayList<Fragment> fragments;lam::ArrayList<Candidate> cells;lam::ArrayList<Column> columns;
    bool allocated=true;for(size_t i=0;i<bake->stats.columns;i++)if(!columns.append({})){allocated=build.fail("navigation bake: column allocation failed");break;}
    bool valid=allocated&&build.phase(NAVIGATION_BAKE_RASTER)&&raster(build,(size_t)x,(size_t)z,fragments)&&
        build.phase(NAVIGATION_BAKE_CLEARANCE)&&candidates(build,(size_t)x,fragments,cells,columns)&&
        build.phase(NAVIGATION_BAKE_EROSION)&&erode(build,(size_t)x,(size_t)z,cells,columns)&&
        build.phase(NAVIGATION_BAKE_GRAPH)&&graph(build,(size_t)x,(size_t)z,cells,columns)&&build.phase(NAVIGATION_BAKE_READY);
    if(valid)*result=bake;else navigation_bake_destroy(bake);return valid;
}
bool navigation_bake_from_cells(const NavigationBakeSettings* settings,const NavigationBakeCell* source,size_t count,NavigationBake** result,NavigationError* error) {
    if(result)*result=nullptr;size_t nx,nz;
    auto fail=[&](const char* message){if(error)*error={message,0};return false;};
    if(!result||(!source&&count))return fail("navigation bake: invalid stored cell buffer");
    if(!navigation_bake_settings_valid(settings,&nx,&nz,error))return false;
    if(count>settings->max_cells)return fail("navigation bake: stored cell quota exceeded");
    void* memory=mem_alloc(sizeof(NavigationBake),MEM_CAT_EVAL);if(!memory)return fail("navigation bake: stored domain allocation failed");
    auto* bake=new(memory) NavigationBake();bake->settings=*settings;bake->stats.columns=nx*nz;bake->stats.candidates=count;
    NavigationBakeDesc desc={nullptr,0,*settings,nullptr,nullptr};Build build={desc,error,*bake};
    lam::ArrayList<Candidate> candidates;lam::ArrayList<Column> columns;bool valid=true;
    for(size_t i=0;i<nx*nz;i++)if(!columns.append({})){valid=fail("navigation bake: column allocation failed");break;}
    size_t previous=0;double previous_floor=0,previous_ceiling=0;
    for(size_t i=0;i<count&&valid;i++) {
        auto cell=source[i];size_t column=cell.x+nx*cell.z;
        if(cell.x>=nx||cell.z>=nz||!isfinite(cell.floor)||!isfinite(cell.ceiling)||cell.floor<settings->low.y||cell.ceiling>settings->high.y||
            cell.ceiling-cell.floor<settings->height||!isfinite(cell.cost)||cell.cost<0||!cell.groups||
            (i&&(column<previous||(column==previous&&(cell.floor-previous_floor<settings->height||previous_ceiling>cell.floor))))) {
            valid=fail("navigation bake: invalid or unordered stored cell");break;
        }
        if(!i||column!=previous)columns[column].begin=i;
        columns[column].end=i+1;previous=column;previous_floor=cell.floor;previous_ceiling=cell.ceiling;
        valid=candidates.append({cell,column,i+1,true})&&bake->cells.append(cell);
        if(!valid)fail("navigation bake: stored cell allocation failed");
    }
    if(valid)valid=graph(build,nx,nz,candidates,columns);
    if(valid)*result=bake;else navigation_bake_destroy(bake);return valid;
}
