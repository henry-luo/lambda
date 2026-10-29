#include "navigation.h"
#include "priority_queue.h"
#include "hashmap_helpers.h"
#include "mem.h"
#include <math.h>
#include <float.h>

typedef struct NIdentity {uint64_t id;size_t index;} NIdentity;
HASHMAP_DEFINE_INTKEY(nidentity,NIdentity,id)
typedef struct NNode {NavigationNodeDesc d;size_t first,count;} NNode;
typedef struct NEdge {size_t from,to,guards[NAVIGATION_MAX_GUARDS];double cost;uint64_t target;uint32_t membership;unsigned guard_count;bool disabled;} NEdge;
struct NavigationGraph {NNode* nodes;NEdge* edges;HashMap* ids;size_t node_count,edge_count;double metric_scale;};
static bool nfail(NavigationError* error,const char* message,uint64_t id) {if(error){error->message=message;error->id=id;}return false;}
size_t navigation_node_index(const NavigationGraph* graph,uint64_t id) {
    if(!graph)return SIZE_MAX;
    NIdentity key={id,0};const NIdentity* value=hashmap_get(graph->ids,&key);return value?value->index:SIZE_MAX;
}
void navigation_destroy(NavigationGraph* graph) {
    if(!graph)return;mem_free(graph->nodes);mem_free(graph->edges);if(graph->ids)hashmap_free(graph->ids);mem_free(graph);
}
static int nedge_compare(const void* left,const void* right) {
    const NEdge *a=left,*b=right;
    if(a->from!=b->from)return a->from>b->from?1:-1;
    if(a->target!=b->target)return a->target>b->target?1:-1;
    return (a->cost>b->cost)-(a->cost<b->cost);
}
bool navigation_create(const NavigationDesc* desc,NavigationGraph** result,NavigationError* error) {
    if(result)*result=NULL;
    if(!desc||!result||(desc->dimensions!=2&&desc->dimensions!=3)||!desc->node_count||desc->node_count>NAVIGATION_MAX_NODES||
        desc->edge_count>NAVIGATION_MAX_EDGES||!desc->nodes||(desc->edge_count&&!desc->edges))return nfail(error,"navigation: invalid dimensions, buffers or graph quota",0);
    NavigationGraph* graph=mem_calloc(1,sizeof(*graph),MEM_CAT_EVAL);if(!graph)return nfail(error,"navigation: graph allocation failed",0);
    graph->node_count=desc->node_count;graph->edge_count=desc->edge_count;graph->metric_scale=DBL_MAX;
    graph->nodes=mem_calloc(desc->node_count,sizeof(NNode),MEM_CAT_EVAL);graph->edges=mem_calloc(desc->edge_count+1,sizeof(NEdge),MEM_CAT_EVAL);graph->ids=nidentity_new(desc->node_count);
    if(!graph->nodes||!graph->edges||!graph->ids){navigation_destroy(graph);return nfail(error,"navigation: graph allocation failed",0);}
    for(size_t i=0;i<desc->node_count;i++) {
        const NavigationNodeDesc* node=&desc->nodes[i];NIdentity key={node->id,i};
        if(!node->id||!navigation_vec_finite(node->position)||(desc->dimensions==2&&node->position.z!=0)||hashmap_get(graph->ids,&key)) {
            navigation_destroy(graph);return nfail(error,"navigation: invalid or duplicate node identity/position",node->id);
        }
        hashmap_set(graph->ids,&key);if(hashmap_oom(graph->ids)){navigation_destroy(graph);return nfail(error,"navigation: identity allocation failed",node->id);}
        graph->nodes[i].d=*node;
    }
    for(size_t i=0;i<desc->edge_count;i++) {
        const NavigationEdgeDesc* source=&desc->edges[i];NEdge* edge=&graph->edges[i];
        edge->from=navigation_node_index(graph,source->from);edge->to=navigation_node_index(graph,source->to);edge->target=source->to;
        if(edge->from==SIZE_MAX||edge->to==SIZE_MAX||!isfinite(source->cost)||source->cost<0||source->guard_count>NAVIGATION_MAX_GUARDS) {
            navigation_destroy(graph);return nfail(error,"navigation: invalid edge reference, cost or guard quota",source->from);
        }
        edge->cost=source->cost;edge->membership=source->membership;edge->disabled=source->disabled;edge->guard_count=source->guard_count;
        for(unsigned k=0;k<source->guard_count;k++) {
            edge->guards[k]=navigation_node_index(graph,source->guards[k]);
            if(edge->guards[k]==SIZE_MAX){navigation_destroy(graph);return nfail(error,"navigation: unknown clearance node",source->guards[k]);}
        }
        double distance=navigation_vec_distance(graph->nodes[edge->from].d.position,graph->nodes[edge->to].d.position);
        if(!isfinite(distance)){navigation_destroy(graph);return nfail(error,"navigation: metric outside numerical range",source->from);}
        if(distance>0)graph->metric_scale=fmin(graph->metric_scale,source->cost/distance);
    }
    // a global lower bound on cost per distance keeps the metric admissible even for weighted/zero-cost edges.
    graph->metric_scale=graph->metric_scale==DBL_MAX?0:nextafter(graph->metric_scale,0);
    qsort(graph->edges,graph->edge_count,sizeof(NEdge),nedge_compare);
    for(size_t i=0;i<graph->edge_count;i++){NNode* node=&graph->nodes[graph->edges[i].from];if(!node->count)node->first=i;node->count++;}
    *result=graph;return true;
}
bool navigation_node(const NavigationGraph* graph,uint64_t id,NavigationNodeDesc* node) {
    if(!graph||!node)return false;size_t index=navigation_node_index(graph,id);if(index==SIZE_MAX)return false;*node=graph->nodes[index].d;return true;
}
bool navigation_node_at(const NavigationGraph* graph,size_t index,NavigationNodeDesc* node) {
    if(!graph||!node||index>=graph->node_count)return false;*node=graph->nodes[index].d;return true;
}
bool navigation_edge_at(const NavigationGraph* graph,size_t index,NavigationEdgeDesc* result) {
    if(!graph||!result||index>=graph->edge_count)return false;const NEdge* edge=&graph->edges[index];
    *result=(NavigationEdgeDesc){.from=graph->nodes[edge->from].d.id,.to=graph->nodes[edge->to].d.id,
        .cost=edge->cost,.membership=edge->membership,.disabled=edge->disabled,.guard_count=edge->guard_count};
    for(unsigned i=0;i<edge->guard_count;i++)result->guards[i]=graph->nodes[edge->guards[i]].d.id;return true;
}
size_t navigation_node_count(const NavigationGraph* graph){return graph?graph->node_count:0;}
size_t navigation_edge_count(const NavigationGraph* graph){return graph?graph->edge_count:0;}
bool navigation_outgoing_edges(const NavigationGraph* graph,uint64_t id,size_t* first,size_t* count) {
    size_t index=navigation_node_index(graph,id);if(index==SIZE_MAX||!first||!count)return false;
    *first=graph->nodes[index].first;*count=graph->nodes[index].count;return true;
}
bool navigation_equal(const NavigationGraph* a,const NavigationGraph* b) {
    if(a==b)return true;if(!a||!b||a->node_count!=b->node_count||a->edge_count!=b->edge_count)return false;
    for(size_t i=0;i<a->node_count;i++) {
        const NavigationNodeDesc *x=&a->nodes[i].d,*y=&b->nodes[i].d;
        if(x->id!=y->id||x->position.x!=y->position.x||x->position.y!=y->position.y||x->position.z!=y->position.z||
            x->membership!=y->membership||x->disabled!=y->disabled||x->transit!=y->transit)return false;
    }
    for(size_t i=0;i<a->edge_count;i++) {
        const NEdge *x=&a->edges[i],*y=&b->edges[i];
        if(x->from!=y->from||x->to!=y->to||x->cost!=y->cost||x->membership!=y->membership||x->disabled!=y->disabled||x->guard_count!=y->guard_count)return false;
        for(unsigned k=0;k<x->guard_count;k++)if(x->guards[k]!=y->guards[k])return false;
    }
    return true;
}
void navigation_path_clear(NavigationPath* path){if(path){mem_free(path->nodes);memset(path,0,sizeof(*path));}}

typedef struct NSearchNode {double cost,score,remaining;size_t parent,queue;uint64_t id;bool reached,closed;} NSearchNode;
static int nsearch_compare(const void* left,const void* right,void* owner) {
    (void)owner;const NSearchNode *a=left,*b=right;
    return a->score!=b->score?(a->score>b->score?1:-1):(a->id>b->id)-(a->id<b->id);
}
static void nsearch_relocate(void* data,size_t index,void* owner){(void)owner;((NSearchNode*)data)->queue=index;}
static bool nadmitted(const NavigationGraph* graph,size_t node,const NavigationQuery* query) {
    const NavigationNodeDesc* desc=&graph->nodes[node].d;
    return !desc->disabled&&(desc->membership&query->filter)&&(!query->admit_node||query->admit_node(query->admission_owner,desc->id));
}
static bool nedge_admitted(const NavigationGraph* graph,const NEdge* edge,const NavigationQuery* query) {
    bool admitted=!edge->disabled&&(edge->membership&query->filter)&&nadmitted(graph,edge->to,query);
    for(unsigned i=0;i<edge->guard_count&&admitted;i++)admitted=nadmitted(graph,edge->guards[i],query);return admitted;
}
bool navigation_node_admitted(const NavigationGraph* graph,uint64_t id,const NavigationQuery* query) {
    size_t index=navigation_node_index(graph,id);return query&&index!=SIZE_MAX&&nadmitted(graph,index,query);
}
bool navigation_edge_admitted(const NavigationGraph* graph,size_t index,const NavigationQuery* query) {
    return graph&&query&&index<graph->edge_count&&nedge_admitted(graph,&graph->edges[index],query);
}
bool navigation_find(const NavigationGraph* graph,const NavigationQuery* query,NavigationPath* result,NavigationError* error) {
    if(!graph||!query||!result)return nfail(error,"navigation: requires graph, query and result",0);
    size_t start=navigation_node_index(graph,query->start),goal=navigation_node_index(graph,query->goal);
    if(start==SIZE_MAX||goal==SIZE_MAX)return nfail(error,"navigation: unknown start or goal",start==SIZE_MAX?query->start:query->goal);
    NavigationPath path={0};path.status=NAVIGATION_UNREACHABLE;
    if(!nadmitted(graph,start,query)||(!query->allow_partial&&!nadmitted(graph,goal,query))){*result=path;return true;}
    if(start==goal) {
        path.nodes=mem_alloc(sizeof(uint64_t),MEM_CAT_EVAL);if(!path.nodes)return nfail(error,"navigation: path allocation failed",query->start);
        path.nodes[0]=query->start;path.count=1;path.status=NAVIGATION_COMPLETE;*result=path;return true;
    }
    NSearchNode* nodes=mem_calloc(graph->node_count,sizeof(NSearchNode),MEM_CAT_EVAL);
    PriorityQueue* queue=priority_queue_create_custom(graph->node_count,nsearch_compare,nsearch_relocate,NULL);
    if(!nodes||!queue){mem_free(nodes);priority_queue_destroy(queue);return nfail(error,"navigation: search allocation failed",0);}
    bool valid=true,exhausted=false;size_t best=start;NavigationVec target=graph->nodes[goal].d.position;
    for(size_t i=0;i<graph->node_count;i++) {
        nodes[i].id=graph->nodes[i].d.id;nodes[i].parent=nodes[i].queue=SIZE_MAX;
        nodes[i].remaining=(query->euclidean||query->allow_partial)?navigation_vec_distance(graph->nodes[i].d.position,target):0;
        nodes[i].score=query->euclidean?nodes[i].remaining*graph->metric_scale:0;
        if(!isfinite(nodes[i].remaining)||!isfinite(nodes[i].score)){valid=nfail(error,"navigation: heuristic outside numerical range",nodes[i].id);break;}
    }
    if(valid){nodes[start].reached=true;valid=priority_queue_push(queue,&nodes[start],0);path.frontier_peak=1;
        if(!valid)nfail(error,"navigation: frontier allocation failed",query->start);}
    while(valid&&!priority_queue_is_empty(queue)) {
        if(path.expanded==query->max_expansions){exhausted=true;break;}
        NSearchNode* current=priority_queue_pop(queue);size_t index=(size_t)(current-nodes);current->closed=true;path.expanded++;
        if(!graph->nodes[index].d.transit&&(current->remaining<nodes[best].remaining||(current->remaining==nodes[best].remaining&&current->id<nodes[best].id)))best=index;
        if(index==goal&&nadmitted(graph,goal,query)){path.status=NAVIGATION_COMPLETE;best=goal;break;}
        const NNode* source=&graph->nodes[index];
        for(size_t k=0;k<source->count;k++) {
            if(path.edge_tests==query->max_edge_tests){exhausted=true;break;}
            const NEdge* edge=&graph->edges[source->first+k];path.edge_tests++;
            bool admitted=nedge_admitted(graph,edge,query);
            NSearchNode* next=&nodes[edge->to];if(!admitted||next->closed)continue;
            double cost=current->cost+edge->cost;
            if(!isfinite(cost)){valid=nfail(error,"navigation: path cost outside numerical range",next->id);break;}
            if(next->reached&&next->cost<=cost)continue;
            double heuristic=query->euclidean?next->remaining*graph->metric_scale:0;
            next->cost=cost;next->score=cost+heuristic;next->parent=index;next->reached=true;
            if(!isfinite(next->score)){valid=nfail(error,"navigation: search score outside numerical range",next->id);break;}
            if(next->queue==SIZE_MAX)valid=priority_queue_push(queue,next,0);else valid=priority_queue_update(queue,next->queue);
            if(!valid){nfail(error,"navigation: frontier allocation failed",next->id);break;}
            path.updates++;if(queue->size>path.frontier_peak)path.frontier_peak=queue->size;
        }
        if(exhausted)break;
    }
    if(valid) {
        if(exhausted)path.status=NAVIGATION_EXHAUSTED;
        else if(path.status!=NAVIGATION_COMPLETE&&query->allow_partial)path.status=NAVIGATION_PARTIAL;
        if(path.status==NAVIGATION_COMPLETE||query->allow_partial) {
            size_t cursor=best;
            do {path.count++;cursor=nodes[cursor].parent;}while(cursor!=SIZE_MAX&&path.count<=graph->node_count);
            if(path.count>graph->node_count)valid=nfail(error,"navigation: cyclic predecessor chain",nodes[best].id);
            if(valid){path.nodes=mem_alloc(path.count*sizeof(uint64_t),MEM_CAT_EVAL);if(!path.nodes)valid=nfail(error,"navigation: path allocation failed",0);}
            if(valid){cursor=best;for(size_t i=path.count;i>0;i--){path.nodes[i-1]=nodes[cursor].id;cursor=nodes[cursor].parent;}path.cost=nodes[best].cost;}
        }
    }
    priority_queue_destroy(queue);mem_free(nodes);
    if(!valid){navigation_path_clear(&path);return false;}*result=path;return true;
}

static size_t ngrid_id(const NavigationGridDesc* grid,const size_t cell[3]){return 1+cell[0]+grid->size[0]*(cell[1]+grid->size[1]*cell[2]);}
static bool ngrid_edges(const NavigationGridDesc* grid,NavigationEdgeDesc* edges,size_t* count,NavigationError* error) {
    *count=0;
    for(size_t z=0;z<grid->size[2];z++)for(size_t y=0;y<grid->size[1];y++)for(size_t x=0;x<grid->size[0];x++) {
        size_t cell[3]={x,y,z};
        for(int dz=grid->dimensions==3?-1:0;dz<=(grid->dimensions==3?1:0);dz++)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++) {
            int delta[3]={dx,dy,dz};unsigned axes=(dx!=0)+(dy!=0)+(dz!=0);
            if(!axes||(!grid->diagonals&&axes>1))continue;
            size_t next[3];bool inside=true;
            for(unsigned a=0;a<3;a++){if((delta[a]<0&&!cell[a])||(delta[a]>0&&cell[a]+1==grid->size[a]))inside=false;next[a]=delta[a]<0?cell[a]-1:cell[a]+(size_t)delta[a];}
            if(!inside)continue;
            if(*count==NAVIGATION_MAX_EDGES)return nfail(error,"navigation: grid edge quota exceeded",0);
            NavigationEdgeDesc edge={0};edge.from=ngrid_id(grid,cell);edge.to=ngrid_id(grid,next);edge.membership=UINT32_MAX;
            double length=hypot(hypot(dx*grid->cell_size.x,dy*grid->cell_size.y),dz*grid->cell_size.z);
            edge.cost=length*(grid->costs?grid->costs[edge.to-1]:1);
            if(!isfinite(length)||!isfinite(edge.cost))return nfail(error,"navigation: grid edge cost outside numerical range",edge.from);
            // every proper subset of a diagonal is a clearance cell, including 3D face/edge neighbors.
            for(unsigned mask=1;mask<8;mask++) {
                bool subset=true,full=true;size_t guard[3];
                for(unsigned a=0;a<3;a++){bool selected=(mask&(1u<<a))!=0;if(selected&&!delta[a])subset=false;if(!selected&&delta[a])full=false;guard[a]=selected?next[a]:cell[a];}
                if(subset&&!full)edge.guards[edge.guard_count++]=ngrid_id(grid,guard);
            }
            if(edges)edges[*count]=edge;(*count)++;
        }
    }
    return true;
}
bool navigation_create_grid(const NavigationGridDesc* grid,NavigationGraph** result,NavigationError* error) {
    if(result)*result=NULL;
    if(!grid||!result||(grid->dimensions!=2&&grid->dimensions!=3)||!grid->size[0]||!grid->size[1]||!grid->size[2]||
        (grid->dimensions==2&&(grid->size[2]!=1||grid->origin.z!=0))||!navigation_vec_finite(grid->origin)||!navigation_vec_finite(grid->cell_size)||
        grid->cell_size.x<=0||grid->cell_size.y<=0||grid->cell_size.z<=0||grid->size[0]>NAVIGATION_MAX_NODES||
        grid->size[1]>NAVIGATION_MAX_NODES/grid->size[0]||grid->size[2]>NAVIGATION_MAX_NODES/(grid->size[0]*grid->size[1]))return nfail(error,"navigation: invalid grid dimensions, spacing or cell quota",0);
    size_t count=grid->size[0]*grid->size[1]*grid->size[2],edge_count=0;
    if(!ngrid_edges(grid,NULL,&edge_count,error))return false;
    NavigationNodeDesc* nodes=mem_calloc(count,sizeof(NavigationNodeDesc),MEM_CAT_EVAL);
    NavigationEdgeDesc* edges=mem_calloc(edge_count+1,sizeof(NavigationEdgeDesc),MEM_CAT_EVAL);
    if(!nodes||!edges){mem_free(nodes);mem_free(edges);return nfail(error,"navigation: grid allocation failed",0);}
    bool valid=true;
    for(size_t z=0;z<grid->size[2]&&valid;z++)for(size_t y=0;y<grid->size[1]&&valid;y++)for(size_t x=0;x<grid->size[0];x++) {
        size_t cell[3]={x,y,z},id=ngrid_id(grid,cell);NavigationNodeDesc* node=&nodes[id-1];node->id=id;
        node->position=(NavigationVec){grid->origin.x+x*grid->cell_size.x,grid->origin.y+y*grid->cell_size.y,grid->origin.z+z*grid->cell_size.z};
        node->membership=grid->memberships?grid->memberships[id-1]:UINT32_MAX;node->disabled=grid->blocked&&grid->blocked[id-1];
        if(grid->costs&&(!isfinite(grid->costs[id-1])||grid->costs[id-1]<0)){valid=nfail(error,"navigation: grid requires nonnegative finite cell costs",id);break;}
    }
    if(valid)valid=ngrid_edges(grid,edges,&edge_count,error);
    NavigationDesc desc={grid->dimensions,nodes,count,edges,edge_count};if(valid)valid=navigation_create(&desc,result,error);
    mem_free(nodes);mem_free(edges);return valid;
}
