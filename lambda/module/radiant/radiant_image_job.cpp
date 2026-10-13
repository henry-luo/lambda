#include "radiant_image_job.hpp"
#include "radiant_dom_bridge.hpp"
#include "../../runtime/image_process.hpp"
#include "../../core/mark_reader.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../../lib/image_process.h"
#include "../../../lib/image.h"
#include "../../../lib/file.h"
#include "../../../lib/thread_pool.h"
#include "../../../lib/time_util.h"
#include "../../../lib/mem.h"
#include <pthread.h>

struct RadiantImageSource {
    ImageProcessSource image;
    uint8_t* pixels;
    char* key;
    int references;
};
struct RadiantImageJob {
    RadiantImageSource* source;
    ImageProcessOptions options;
    ImageProcessResult result;
    uint64_t token, elapsed_us;
    uint64_t encode_us;
    char* destination;
    int destination_created;
    bool claimed;
    int channel;
    bool cancelled, success;
};
struct RadiantImageJobs;
struct RadiantImageWorker { RadiantImageJobs* owner; int channel; };
struct RadiantImageJobs : DomDocumentResourceData {
    ThreadPool* pool;
    pthread_mutex_t mutex;
    RadiantImageSource* sources[2];
    RadiantImageJob* pending[2];
    RadiantImageJob* completed[2];
    RadiantImageJob* active[2];
    RadiantImageWorker workers[2];
    ImageProcessCache* cache;
    uint64_t token, source_revision, latest[2];
    bool running[2], closing;
};
static void radiant_image_source_release(RadiantImageSource* source) {
    if (source && __atomic_sub_fetch(&source->references,1,__ATOMIC_ACQ_REL)==0) {
        mem_free(source->pixels); mem_free(source->key); mem_free(source);
    }
}
static void radiant_image_job_free(RadiantImageJob* job) {
    if (!job) return;
    image_process_result_free(&job->result);
    if (job->destination_created && !job->claimed) file_delete(job->destination);
    mem_free(job->destination);
    radiant_image_source_release(job->source);
    mem_free(job);
}
static void radiant_image_worker(void* opaque) {
    auto& worker=*(RadiantImageWorker*)opaque;
    auto* resource=worker.owner;
    int channel=worker.channel;
    while (true) {
        pthread_mutex_lock(&resource->mutex);
        RadiantImageJob* job=resource->pending[channel];
        resource->pending[channel]=nullptr;
        if (!job) {
            resource->running[channel]=false;
            pthread_mutex_unlock(&resource->mutex); return;
        }
        resource->active[channel]=job;
        pthread_mutex_unlock(&resource->mutex);
        uint64_t began=time_now_us();
        job->success=image_process(job->source->image,job->options,&job->result,
            channel==0 ? resource->cache : nullptr,&job->cancelled);
        job->elapsed_us=time_now_us()-began;
        if (job->success && job->destination) {
            began=time_now_us();
            auto cancelled=[](void* data) -> int {
                return __atomic_load_n(&((RadiantImageJob*)data)->cancelled,__ATOMIC_RELAXED);
            };
            job->success=image_save_png_cancelable(job->destination,job->result.pixels,
                job->result.width,job->result.height,4,cancelled,job,&job->destination_created)!=0;
            job->encode_us=time_now_us()-began;
            // encoded jobs never need to copy their full image back to the GC heap.
            image_process_result_free(&job->result);
        }
        pthread_mutex_lock(&resource->mutex);
        resource->active[channel]=nullptr;
        if (!resource->closing && job->token==resource->latest[channel] &&
            !__atomic_load_n(&job->cancelled,__ATOMIC_RELAXED)) {
            radiant_image_job_free(resource->completed[channel]);
            resource->completed[channel]=job;
        } else radiant_image_job_free(job);
        pthread_mutex_unlock(&resource->mutex);
    }
}
static void radiant_image_jobs_destroy(DomDocumentResourceData* data) {
    auto* resource=(RadiantImageJobs*)data;
    pthread_mutex_lock(&resource->mutex);
    resource->closing=true;
    for (int i=0;i<2;i++) {
        if (resource->active[i]) __atomic_store_n(&resource->active[i]->cancelled,true,__ATOMIC_RELAXED);
        radiant_image_job_free(resource->pending[i]); resource->pending[i]=nullptr;
    }
    pthread_mutex_unlock(&resource->mutex);
    // workers only own native buffers; cancellation is checked at every scanline.
    tp_destroy(resource->pool);
    for (int i=0;i<2;i++) {
        radiant_image_job_free(resource->completed[i]);
        radiant_image_source_release(resource->sources[i]);
    }
    image_process_cache_destroy(resource->cache);
    pthread_mutex_destroy(&resource->mutex);
    mem_free(resource);
}
static RadiantImageJobs* radiant_image_jobs(DomDocument* doc, bool create) {
    if (!doc) return nullptr;
    for (DomDocumentResource* entry=doc->resources;entry;entry=entry->next)
        if (entry->destroy==radiant_image_jobs_destroy) return (RadiantImageJobs*)entry->data;
    if (!create) return nullptr;
    auto* resource=(RadiantImageJobs*)mem_calloc(1,sizeof(RadiantImageJobs),MEM_CAT_TEMP);
    if (!resource) return nullptr;
    if (pthread_mutex_init(&resource->mutex,nullptr)!=0) { mem_free(resource); return nullptr; }
    resource->pool=tp_create(2);
    for (int i=0;i<2;i++) resource->workers[i]={resource,i};
    resource->cache=image_process_cache_create(64u*1024u*1024u);
    if (!resource->pool || !resource->cache || !dom_document_add_resource(doc,resource,radiant_image_jobs_destroy)) {
        radiant_image_jobs_destroy(resource); return nullptr;
    }
    dom_document_set_resource_pending_work(doc,resource,[](DomDocumentResourceData* data) -> bool {
        auto* jobs=(RadiantImageJobs*)data;
        pthread_mutex_lock(&jobs->mutex);
        bool pending=jobs->running[0] || jobs->running[1] || jobs->completed[0] || jobs->completed[1];
        pthread_mutex_unlock(&jobs->mutex);
        return pending;
    });
    return resource;
}
static RadiantImageSource* radiant_image_source(RadiantImageJobs* resource, int channel,
    Item pixels, const char* key) {
    ImageProcessSource image;
    if (!key || strlen(key)>1024 || !lambda_image_source(pixels,&image)) return nullptr;
    auto* source=resource->sources[channel];
    if (source && source->image.width==image.width && source->image.height==image.height && strcmp(source->key,key)==0) return source;
    source=(RadiantImageSource*)mem_calloc(1,sizeof(RadiantImageSource),MEM_CAT_TEMP);
    if (!source) return nullptr;
    size_t bytes=(size_t)image.width*image.height*4;
    source->pixels=(uint8_t*)mem_alloc(bytes,MEM_CAT_TEMP);
    source->key=(char*)mem_alloc(strlen(key)+1,MEM_CAT_TEMP);
    source->references=1;
    if (!source->pixels || !source->key) { radiant_image_source_release(source); return nullptr; }
    strcpy(source->key,key);
    for (int y=0;y<image.height;y++) {
        if (image.strides[1]==4 && image.strides[2]==1)
            memcpy(source->pixels+(size_t)y*image.width*4,image.pixels+y*image.strides[0],(size_t)image.width*4);
        else for (int x=0;x<image.width;x++) for (int c=0;c<4;c++)
            source->pixels[((size_t)y*image.width+x)*4+c]=image.pixels[y*image.strides[0]+x*image.strides[1]+c*image.strides[2]];
    }
    source->image={source->pixels,image.width,image.height,{(int64_t)image.width*4,4,1},++resource->source_revision};
    radiant_image_source_release(resource->sources[channel]);
    resource->sources[channel]=source;
    return source;
}
extern "C" Item fn_radiant_image_job(Item owner, Item request) {
    RootFrame roots(3); Rooted<Item> node(roots,owner), input(roots,request), output(roots,ItemNull);
    DomDocument* doc=radiant_dom_item_document(node.get());
    if (!doc || get_type_id(input.get())!=LMD_TYPE_MAP) return ItemError;
    MapReader args=MapReader::fromItem(input.get());
    const char* action=args.get("action").cstring();
    const char* name=args.get("channel").cstring();
    if (!action || !name || (strcmp(name,"preview")!=0 && strcmp(name,"export")!=0)) return ItemError;
    int channel=strcmp(name,"export")==0 ? 1 : 0;
    RadiantImageJobs* resource=radiant_image_jobs(doc,strcmp(action,"start")==0);
    if (!resource) return strcmp(action,"start")==0 ? ItemError : ItemNull;
    if (strcmp(action,"start")==0) {
        auto* job=(RadiantImageJob*)mem_calloc(1,sizeof(RadiantImageJob),MEM_CAT_TEMP);
        if (!job) return ItemError;
        if (!lambda_image_options(args.get("options").item(),&job->options)) { mem_free(job); return ItemError; }
        auto destination=args.get("destination");
        if (!destination.isNull()) {
            if (channel!=1 || !destination.isString() || !destination.cstring()[0]) { mem_free(job); return ItemError; }
            job->destination=mem_strdup(destination.cstring(),MEM_CAT_TEMP);
            if (!job->destination) { mem_free(job); return ItemError; }
        }
        job->source=radiant_image_source(resource,channel,args.get("pixels").item(),args.get("source").cstring());
        if (!job->source) { radiant_image_job_free(job); return ItemError; }
        __atomic_add_fetch(&job->source->references,1,__ATOMIC_RELAXED);
        job->channel=channel;
        pthread_mutex_lock(&resource->mutex);
        job->token=++resource->token;
        resource->latest[channel]=job->token;
        if (resource->active[channel]) __atomic_store_n(&resource->active[channel]->cancelled,true,__ATOMIC_RELAXED);
        radiant_image_job_free(resource->pending[channel]);
        radiant_image_job_free(resource->completed[channel]); resource->completed[channel]=nullptr;
        resource->pending[channel]=job;
        if (!resource->running[channel]) {
            resource->running[channel]=true;
            if (!tp_submit(resource->pool,radiant_image_worker,&resource->workers[channel])) {
                resource->running[channel]=false; resource->pending[channel]=nullptr;
                pthread_mutex_unlock(&resource->mutex); radiant_image_job_free(job); return ItemError;
            }
        }
        uint64_t token=job->token;
        pthread_mutex_unlock(&resource->mutex);
        return {.item=i2it((int64_t)token)};
    }
    if (strcmp(action,"cancel")==0) {
        pthread_mutex_lock(&resource->mutex);
        resource->latest[channel]=++resource->token;
        if (resource->active[channel]) __atomic_store_n(&resource->active[channel]->cancelled,true,__ATOMIC_RELAXED);
        radiant_image_job_free(resource->pending[channel]); resource->pending[channel]=nullptr;
        radiant_image_job_free(resource->completed[channel]); resource->completed[channel]=nullptr;
        pthread_mutex_unlock(&resource->mutex);
        return {.item=b2it(true)};
    }
    if (strcmp(action,"poll")!=0 || !args.get("token").isInt()) return ItemError;
    uint64_t token=(uint64_t)args.get("token").asInt();
    pthread_mutex_lock(&resource->mutex);
    RadiantImageJob* job=resource->completed[channel];
    if (job && job->token==token) resource->completed[channel]=nullptr; else job=nullptr;
    pthread_mutex_unlock(&resource->mutex);
    if (!job) return ItemNull;
    output.set(radiant_obj_new());
    radiant_rooted_obj_set(output,"success",{.item=b2it(job->success)});
    if (job->success) {
        if (!job->destination) radiant_rooted_obj_set(output,"pixels",lambda_image_result(job->result));
        job->claimed=true;
        struct Metric { const char* name; uint64_t value; };
        Metric metrics[]={{"token",job->token},{"elapsed_us",job->elapsed_us},{"encode_us",job->encode_us},
            {"width",(uint64_t)job->result.width},{"height",(uint64_t)job->result.height},{"scratch_bytes",job->result.scratch_bytes},
            {"cache_bytes",job->result.cache_bytes},{"geometry_hits",job->result.geometry_hits},
            {"color_hits",job->result.color_hits},{"blur_hits",job->result.blur_hits}};
        for (auto& metric:metrics) radiant_rooted_obj_set(output,metric.name,{.item=i2it((int64_t)metric.value)});
    } else radiant_rooted_obj_set(output,"pixels",ItemError);
    radiant_image_job_free(job);
    return output.get();
}
