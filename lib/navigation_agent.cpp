#include "navigation.h"
#include "arraylist.hpp"
#include "mem.h"

struct NavigationFollower {
    NavigationFollowerDesc desc={};NavigationFollowState state={};
    lam::ArrayList<NavigationVec> points;lam::ArrayList<NavigationTraversal> traversals;
    double progress_distance=0;bool progress_set=false;
};
static bool follow_fail(NavigationError* error,const char* message){if(error)*error={message,0};return false;}
static bool follow_point(unsigned dimensions,NavigationVec point){return navigation_vec_finite(point)&&(dimensions==3||point.z==0);}
static bool follow_same_point(NavigationVec a,NavigationVec b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
static NavigationVec follow_scale(NavigationVec value,double scale){return {value.x*scale,value.y*scale,value.z*scale};}
static NavigationVec follow_divide(NavigationVec value,double divisor){return {value.x/divisor,value.y/divisor,value.z/divisor};}
static NavigationVec follow_difference(NavigationVec a,NavigationVec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static double follow_length(NavigationVec value){return hypot(hypot(value.x,value.y),value.z);}
static NavigationVec follow_add(NavigationVec a,NavigationVec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static bool follow_desc_valid(const NavigationFollowerDesc* d) {
    return d&&(d->dimensions==2||d->dimensions==3)&&isfinite(d->max_speed)&&d->max_speed>=0&&
        isfinite(d->max_acceleration)&&d->max_acceleration>0&&isfinite(d->waypoint_distance)&&d->waypoint_distance>=0&&
        isfinite(d->target_distance)&&d->target_distance>=0&&isfinite(d->path_distance)&&
        d->path_distance>=fmax(d->waypoint_distance,d->target_distance)&&isfinite(d->stuck_time)&&d->stuck_time>=0&&
        isfinite(d->min_progress)&&d->min_progress>0&&d->max_waypoints&&d->max_waypoints<=NAVIGATION_MAX_ROUTE_POINTS;
}
bool navigation_follower_create(const NavigationFollowerDesc* desc,NavigationFollower** result,NavigationError* error) {
    if(result)*result=nullptr;if(!result||!follow_desc_valid(desc))return follow_fail(error,"navigation follower: invalid description or work budget");
    void* memory=mem_alloc(sizeof(NavigationFollower),MEM_CAT_EVAL);if(!memory)return follow_fail(error,"navigation follower: allocation failed");
    auto* follower=new(memory) NavigationFollower();follower->desc=*desc;*result=follower;return true;
}
void navigation_follower_destroy(NavigationFollower* follower){if(follower){follower->~NavigationFollower();mem_free(follower);}}
void navigation_follower_cancel(NavigationFollower* follower) {
    if(!follower)return;follower->points.clear();follower->traversals.clear();follower->state={};follower->progress_set=false;
}
bool navigation_follower_read(const NavigationFollower* follower,NavigationFollowState* result){if(!follower||!result)return false;*result=follower->state;return true;}
bool navigation_follower_action(const NavigationFollower* follower,NavigationTraversal* action) {
    if(!follower||!action||follower->state.status!=NAVIGATION_FOLLOW_ACTION||follower->state.traversal>=follower->traversals.size())return false;
    *action=follower->traversals[follower->state.traversal];return true;
}
bool navigation_follower_assign(NavigationFollower* follower,const NavigationMeshPath* path,uint64_t generation,NavigationError* error) {
    if(!follower||!path||!generation||!path->points||!path->point_count||path->point_count>NAVIGATION_MAX_ROUTE_POINTS||
        (unsigned)path->corridor.status>NAVIGATION_EXHAUSTED||path->corridor.status==NAVIGATION_UNREACHABLE||
        path->traversal_count>NAVIGATION_MAX_LINKS||(!path->traversals&&path->traversal_count))return follow_fail(error,"navigation follower: requires a verified route and generation");
    lam::ArrayList<NavigationVec> points;lam::ArrayList<NavigationTraversal> actions;
    for(size_t i=0;i<path->point_count;i++) {
        if(!follow_point(follower->desc.dimensions,path->points[i]))return follow_fail(error,"navigation follower: invalid route coordinates");
        if(i&&!isfinite(navigation_vec_distance(path->points[i-1],path->points[i])))return follow_fail(error,"navigation follower: route distance outside numerical range");
        if(!points.append(path->points[i]))return follow_fail(error,"navigation follower: route allocation failed");
    }
    for(size_t i=0;i<path->traversal_count;i++) {
        const auto& action=path->traversals[i];
        if(!action.id||action.start_point>=points.size()||action.end_point!=action.start_point+1||action.end_point>=points.size()||
            (i&&action.start_point<path->traversals[i-1].end_point)||!follow_same_point(action.start,points[action.start_point])||
            !follow_same_point(action.end,points[action.end_point])||!isfinite(action.duration)||action.duration<0||!isfinite(action.cost)||action.cost<0)
            return follow_fail(error,"navigation follower: invalid action endpoints or ordering");
        if(!actions.append(action))return follow_fail(error,"navigation follower: action allocation failed");
    }
    follower->points=static_cast<lam::ArrayList<NavigationVec>&&>(points);
    follower->traversals=static_cast<lam::ArrayList<NavigationTraversal>&&>(actions);
    follower->state={};follower->state.status=NAVIGATION_FOLLOW_MOVING;follower->state.generation=generation;
    follower->state.complete=path->corridor.status==NAVIGATION_COMPLETE;follower->progress_set=false;return true;
}
static double follow_segment_distance(NavigationVec point,NavigationVec a,NavigationVec b) {
    NavigationVec edge=follow_difference(b,a);double length=follow_length(edge);if(length==0)return navigation_vec_distance(point,a);
    if(!isfinite(length))return INFINITY;NavigationVec direction=follow_divide(edge,length),offset=follow_difference(point,a);
    double along=offset.x*direction.x+offset.y*direction.y+offset.z*direction.z;
    if(!isfinite(along))return INFINITY;double t=fmax(0,fmin(1,along/length));
    // convex interpolation avoids overflowing a+(b-a)*t for opposite extreme endpoints.
    NavigationVec nearest=follow_add(follow_scale(a,1-t),follow_scale(b,t));return navigation_vec_distance(point,nearest);
}
static void follow_stop(NavigationFollowState* state,NavigationFollowStatus status) {
    state->status=status;state->velocity={};state->displacement={};if(status!=NAVIGATION_FOLLOW_ACTION)state->action=0;
}
static bool follow_update(const NavigationFollower* follower,NavigationVec position,uint64_t generation,double delta,
        NavigationFollowState* state,double* progress_distance,bool* progress_set,NavigationError* error) {
    const auto& desc=follower->desc;state->position=position;state->waypoint_tests=0;state->budget_exhausted=false;state->displacement={};
    if(state->status==NAVIGATION_FOLLOW_IDLE)return true;
    if(generation!=state->generation){follow_stop(state,NAVIGATION_FOLLOW_STALE);return true;}
    if(state->status!=NAVIGATION_FOLLOW_MOVING)return true;
    const auto& points=follower->points;const auto& actions=follower->traversals;
    while(state->waypoint<points.size()) {
        if(state->waypoint_tests==desc.max_waypoints){state->budget_exhausted=true;follow_stop(state,NAVIGATION_FOLLOW_MOVING);return true;}
        state->waypoint_tests++;size_t cursor=state->waypoint;state->distance=navigation_vec_distance(position,points[cursor]);
        if(!isfinite(state->distance))return follow_fail(error,"navigation follower: observed distance outside numerical range");
        bool last=cursor+1==points.size();double tolerance=last?desc.target_distance:desc.waypoint_distance;
        if(state->distance>tolerance)break;
        if(state->traversal<actions.size()&&actions[state->traversal].start_point==cursor) {
            state->action=actions[state->traversal].id;follow_stop(state,NAVIGATION_FOLLOW_ACTION);return true;
        }
        if(last){follow_stop(state,state->complete?NAVIGATION_FOLLOW_ARRIVED:NAVIGATION_FOLLOW_PATH_END);return true;}
        state->waypoint++;state->stalled_time=0;*progress_set=false;
    }
    size_t cursor=state->waypoint;
    double off_path=cursor?follow_segment_distance(position,points[cursor-1],points[cursor]):state->distance;
    if(off_path>desc.path_distance){follow_stop(state,NAVIGATION_FOLLOW_OFF_PATH);return true;}
    if(!*progress_set||*progress_distance-state->distance>=desc.min_progress) {
        *progress_distance=state->distance;*progress_set=true;state->stalled_time=0;
    }else {
        state->stalled_time+=delta;if(!isfinite(state->stalled_time))return follow_fail(error,"navigation follower: stalled time outside numerical range");
        if(desc.stuck_time>0&&desc.max_speed>0&&state->stalled_time>=desc.stuck_time){follow_stop(state,NAVIGATION_FOLLOW_STUCK);return true;}
    }
    NavigationVec offset=follow_difference(points[cursor],position);double speed=fmin(desc.max_speed,state->distance/delta);
    // braking limits approach speed; infinity here simply leaves the finite speed cap authoritative.
    double braking=sqrt(2.0)*sqrt(desc.max_acceleration)*sqrt(state->distance);speed=fmin(speed,braking);
    NavigationVec desired=follow_scale(follow_divide(offset,state->distance),speed),change=follow_difference(desired,state->velocity);
    double change_length=follow_length(change),limit=desc.max_acceleration*delta;
    if(!isfinite(change_length))return follow_fail(error,"navigation follower: steering outside numerical range");
    state->velocity=change_length>limit?follow_add(state->velocity,follow_scale(change,limit/change_length)):desired;
    state->displacement=follow_scale(state->velocity,delta);double travel=follow_length(state->displacement);
    // never issue a step longer than the next route segment; safety stops can brake immediately.
    if(travel>state->distance){state->displacement=follow_scale(state->displacement,state->distance/travel);state->velocity=follow_divide(state->displacement,delta);}
    return (navigation_vec_finite(state->velocity)&&navigation_vec_finite(state->displacement))||follow_fail(error,"navigation follower: movement outside numerical range");
}
bool navigation_follower_step(NavigationFollower* follower,NavigationVec position,uint64_t generation,double delta,NavigationFollowState* result,NavigationError* error) {
    if(!follower||!result||!follow_point(follower->desc.dimensions,position)||!isfinite(delta)||delta<=0)
        return follow_fail(error,"navigation follower: invalid observation or timestep");
    NavigationFollowState next=follower->state;double progress=follower->progress_distance;bool set=follower->progress_set;
    if(!follow_update(follower,position,generation,delta,&next,&progress,&set,error))return false;
    follower->state=next;follower->progress_distance=progress;follower->progress_set=set;*result=next;return true;
}
bool navigation_follower_complete_link(NavigationFollower* follower,uint64_t generation,uint64_t action,NavigationVec position,NavigationError* error) {
    if(!follower||!follow_point(follower->desc.dimensions,position)||follower->state.status!=NAVIGATION_FOLLOW_ACTION||
        generation!=follower->state.generation||action!=follower->state.action||follower->state.traversal>=follower->traversals.size())
        return follow_fail(error,"navigation follower: stale or inactive action acknowledgement");
    const auto& traversal=follower->traversals[follower->state.traversal];
    if(navigation_vec_distance(position,traversal.end)>follower->desc.waypoint_distance)return follow_fail(error,"navigation follower: action has not achieved its arrival endpoint");
    auto& state=follower->state;state.waypoint=traversal.end_point;state.traversal++;state.action=0;state.position=position;state.stalled_time=0;
    follow_stop(&state,NAVIGATION_FOLLOW_MOVING);follower->progress_set=false;return true;
}
