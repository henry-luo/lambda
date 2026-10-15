#include "../../io/image_io.h"
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
#include "../../../radiant/view.hpp"
#include <math.h>
#include <pthread.h>

enum { IMAGE_JOB_PREVIEW, IMAGE_JOB_EXPORT, IMAGE_JOB_THUMBNAIL, IMAGE_JOB_CHANNELS };
static constexpr size_t THUMBNAIL_CACHE_BYTES = 32u * 1024u * 1024u;
static constexpr unsigned THUMBNAIL_CACHE_ENTRIES = 128;

struct RadiantThumbnailCache {
    RadiantThumbnailCache* next;
    char* path;
    FileRevision revision;
    uint32_t width, height;
    ImageDecoded image;
    ImageError error;
    bool success;
};

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
    char* path;
    ImageDecodeOptions decode;
    ImageInfo info;
    ImageError error;
    uint64_t cache_epoch;
    bool cache_hit;
};
struct RadiantImageJobs;
struct RadiantImageWorker { RadiantImageJobs* owner; int channel; };
struct RadiantImageJobs : DomDocumentResourceData {
    ThreadPool* pool;
    pthread_mutex_t mutex;
    RadiantImageSource* sources[IMAGE_JOB_CHANNELS];
    RadiantImageJob* pending[IMAGE_JOB_CHANNELS];
    RadiantImageJob* completed[IMAGE_JOB_CHANNELS];
    RadiantImageJob* active[IMAGE_JOB_CHANNELS];
    RadiantImageWorker workers[IMAGE_JOB_CHANNELS];
    ImageProcessCache* cache;
    uint64_t token, source_revision, latest[IMAGE_JOB_CHANNELS];
    bool running[IMAGE_JOB_CHANNELS], closing;
    RadiantThumbnailCache* thumbnails;
    size_t thumbnail_bytes, thumbnail_budget;
    unsigned thumbnail_count;
    uint64_t cache_epoch;
};

static void radiant_thumbnail_free(RadiantThumbnailCache* entry) {
    image_decoded_dispose(&entry->image);
    mem_free(entry->path);
    mem_free(entry);
}
static void radiant_thumbnail_clear(RadiantImageJobs* resource) {
    while (resource->thumbnails) {
        auto* entry = resource->thumbnails;
        resource->thumbnails = entry->next;
        radiant_thumbnail_free(entry);
    }
    resource->thumbnail_bytes = 0;
    resource->thumbnail_count = 0;
}
static void radiant_thumbnail_trim(RadiantImageJobs* resource) {
    while (resource->thumbnail_bytes > resource->thumbnail_budget || resource->thumbnail_count > THUMBNAIL_CACHE_ENTRIES) {
        auto** tail = &resource->thumbnails;
        while ((*tail)->next) tail = &(*tail)->next;
        auto* retired = *tail; *tail = nullptr;
        resource->thumbnail_bytes -= retired->image.size; resource->thumbnail_count--;
        radiant_thumbnail_free(retired);
    }
}
static bool radiant_thumbnail_revision(const FileRevision& a, const FileRevision& b) {
    return a.stat.exists && b.stat.exists && a.stat.size == b.stat.size && a.stat.modified == b.stat.modified &&
        a.modified_nsec == b.modified_nsec && a.stat.created == b.stat.created &&
        a.device == b.device && a.inode == b.inode;
}
static bool radiant_thumbnail_copy(const ImageDecoded& source, RadiantImageJob* job) {
    job->result.pixels = (uint8_t*)mem_alloc(source.size, MEM_CAT_IMAGE);
    if (!job->result.pixels) {
        job->error.code = IMAGE_OUT_OF_MEMORY;
        const char* message = "cannot allocate thumbnail pixels";
        str_copy(job->error.message, sizeof(job->error.message), message, strlen(message));
        return false;
    }
    memcpy(job->result.pixels, source.pixels, source.size);
    job->result.width = source.width;
    job->result.height = source.height;
    job->result.owns_pixels = true;
    job->info = source.info;
    return true;
}
static void radiant_thumbnail_decode(RadiantImageJobs* resource, RadiantImageJob* job) {
    char* canonical = file_realpath(job->path);
    if (canonical) { mem_free(job->path); job->path = canonical; }
    FileRevision before = file_revision(job->path);
    pthread_mutex_lock(&resource->mutex);
    RadiantThumbnailCache** link = &resource->thumbnails;
    while (*link) {
        auto* entry = *link;
        if (entry->width == job->decode.target_width && entry->height == job->decode.target_height &&
            strcmp(entry->path, job->path) == 0 && radiant_thumbnail_revision(before, entry->revision)) {
            *link = entry->next; entry->next = resource->thumbnails; resource->thumbnails = entry;
            job->error = entry->error;
            job->success = entry->success && radiant_thumbnail_copy(entry->image, job);
            job->cache_hit = true;
            pthread_mutex_unlock(&resource->mutex);
            return;
        }
        link = &entry->next;
    }
    pthread_mutex_unlock(&resource->mutex);
    ImageContext context; image_context_init(&context);
    context.cancelled = [](void* opaque) -> int {
        return __atomic_load_n(&((RadiantImageJob*)opaque)->cancelled, __ATOMIC_RELAXED);
    };
    context.user = job;
    ImageDecoded image = {};
    job->success = image_io_decode_path(&context, job->path, &job->decode, &image, &job->error) == IMAGE_OK;
    FileRevision after = file_revision(job->path);
    // never cache or publish pixels decoded while their source was changing.
    if (job->success && !radiant_thumbnail_revision(before, after)) {
        job->success = false;
        job->error.code = IMAGE_IO_ERROR;
        const char* message = "image changed during thumbnail loading";
        str_copy(job->error.message, sizeof(job->error.message), message, strlen(message));
    }
    if (job->success) job->success = radiant_thumbnail_copy(image, job);
    pthread_mutex_lock(&resource->mutex);
    if (job->cache_epoch == resource->cache_epoch && !context.cancelled(job) &&
        before.stat.exists && image.size <= resource->thumbnail_budget && radiant_thumbnail_revision(before, after)) {
        auto* entry = (RadiantThumbnailCache*)mem_calloc(1, sizeof(RadiantThumbnailCache), MEM_CAT_IMAGE);
        if (entry) {
            entry->path = mem_strdup(job->path, MEM_CAT_IMAGE);
            if (!entry->path) mem_free(entry);
            else {
                entry->revision = after; entry->width = job->decode.target_width; entry->height = job->decode.target_height;
                entry->success = job->success; entry->error = job->error;
                entry->image = image; image = {};
                entry->next = resource->thumbnails; resource->thumbnails = entry;
                resource->thumbnail_bytes += entry->image.size; resource->thumbnail_count++;
                radiant_thumbnail_trim(resource);
            }
        }
    }
    pthread_mutex_unlock(&resource->mutex);
    image_decoded_dispose(&image);
}

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
    mem_free(job->path);
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
        if (job->path) radiant_thumbnail_decode(resource, job);
        else job->success=image_process(job->source->image,job->options,&job->result,
            channel==IMAGE_JOB_PREVIEW ? resource->cache : nullptr,&job->cancelled);
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
    for (int i=0;i<IMAGE_JOB_CHANNELS;i++) {
        if (resource->active[i]) __atomic_store_n(&resource->active[i]->cancelled,true,__ATOMIC_RELAXED);
        radiant_image_job_free(resource->pending[i]); resource->pending[i]=nullptr;
    }
    pthread_mutex_unlock(&resource->mutex);
    // workers only own native buffers; cancellation is checked at every scanline.
    tp_destroy(resource->pool);
    for (int i=0;i<IMAGE_JOB_CHANNELS;i++) {
        radiant_image_job_free(resource->completed[i]);
        radiant_image_source_release(resource->sources[i]);
    }
    image_process_cache_destroy(resource->cache);
    radiant_thumbnail_clear(resource);
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
    for (int i=0;i<IMAGE_JOB_CHANNELS;i++) resource->workers[i]={resource,i};
    resource->cache=image_process_cache_create(64u*1024u*1024u);
    resource->thumbnail_budget=THUMBNAIL_CACHE_BYTES;
    if (!resource->pool || !resource->cache || !dom_document_add_resource(doc,resource,radiant_image_jobs_destroy)) {
        radiant_image_jobs_destroy(resource); return nullptr;
    }
    dom_document_set_resource_pending_work(doc,resource,[](DomDocumentResourceData* data) -> bool {
        auto* jobs=(RadiantImageJobs*)data;
        pthread_mutex_lock(&jobs->mutex);
        bool pending=false;
        for (int i=0;i<IMAGE_JOB_CHANNELS;i++) pending |= jobs->running[i] || jobs->completed[i];
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
    RootFrame roots(4); Rooted<Item> node(roots,owner), input(roots,request), output(roots,ItemNull), error(roots,ItemNull);
    DomDocument* doc=radiant_dom_item_document(node.get());
    if (!doc || get_type_id(input.get())!=LMD_TYPE_MAP) return ItemError;
    MapReader args=MapReader::fromItem(input.get());
    const char* action=args.get("action").cstring();
    const char* name=args.get("channel").cstring();
    if (!action || !name) return ItemError;
    int channel=strcmp(name,"preview")==0 ? IMAGE_JOB_PREVIEW : strcmp(name,"export")==0 ? IMAGE_JOB_EXPORT :
        strcmp(name,"thumbnail")==0 ? IMAGE_JOB_THUMBNAIL : -1;
    if (channel<0) return ItemError;
    RadiantImageJobs* resource=radiant_image_jobs(doc,strcmp(action,"start")==0);
    if (!resource) return strcmp(action,"start")==0 ? ItemError : ItemNull;
    if (strcmp(action,"start")==0) {
        auto* job=(RadiantImageJob*)mem_calloc(1,sizeof(RadiantImageJob),MEM_CAT_TEMP);
        if (!job) return ItemError;
        size_t thumbnail_budget=THUMBNAIL_CACHE_BYTES;
        if (channel==IMAGE_JOB_THUMBNAIL) {
            const char* path=args.get("path").cstring();
            auto width=args.get("width"), height=args.get("height");
            if (!path || !*path || !width.isNumber() || !height.isNumber()) { mem_free(job); return ItemError; }
            double w=width.isInt() ? (double)width.asInt() : width.asFloat();
            double h=height.isInt() ? (double)height.asInt() : height.asFloat();
            UiContext* ui=static_cast<UiContext*>(doc->js.host_ui_context);
            double scale=ui ? ui_context_raster_scale(ui) : 1.0;
            w=ceil(w*scale); h=ceil(h*scale);
            if (!isfinite(w) || !isfinite(h) || w<1 || h<1 || w>4096 || h>4096 || w*h>4194304) { mem_free(job); return ItemError; }
            auto retained=args.get("canvas_bytes");
            if (!retained.isNull() && (!retained.isInt() || retained.asInt()<0)) { mem_free(job); return ItemError; }
            uint64_t reserved=(retained.isNull() ? 0 : (uint64_t)retained.asInt())+(uint64_t)(w*h)*4;
            // reserve existing canvases and this upload before keeping any cached pixels.
            thumbnail_budget=reserved>=THUMBNAIL_CACHE_BYTES ? 0 : THUMBNAIL_CACHE_BYTES-(size_t)reserved;
            job->path=mem_strdup(path,MEM_CAT_IMAGE);
            if (!job->path) { mem_free(job); return ItemError; }
            image_decode_options_init(&job->decode);
            job->decode.mode=IMAGE_DECODE_THUMBNAIL;
            job->decode.target_width=(uint32_t)w; job->decode.target_height=(uint32_t)h;
            job->decode.normalize_orientation=true;
        } else if (!lambda_image_options(args.get("options").item(),&job->options)) { mem_free(job); return ItemError; }
        auto destination=args.get("destination");
        if (!destination.isNull()) {
            if (channel!=IMAGE_JOB_EXPORT || !destination.isString() || !destination.cstring()[0]) { radiant_image_job_free(job); return ItemError; }
            job->destination=mem_strdup(destination.cstring(),MEM_CAT_TEMP);
            if (!job->destination) { mem_free(job); return ItemError; }
        }
        if (channel!=IMAGE_JOB_THUMBNAIL) {
            job->source=radiant_image_source(resource,channel,args.get("pixels").item(),args.get("source").cstring());
            if (!job->source) { radiant_image_job_free(job); return ItemError; }
            __atomic_add_fetch(&job->source->references,1,__ATOMIC_RELAXED);
        }
        job->channel=channel;
        pthread_mutex_lock(&resource->mutex);
        job->token=++resource->token;
        job->cache_epoch=resource->cache_epoch;
        if (channel==IMAGE_JOB_THUMBNAIL) {
            resource->thumbnail_budget=thumbnail_budget;
            radiant_thumbnail_trim(resource);
        }
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
    if (strcmp(action,"cancel")==0 || (channel==IMAGE_JOB_THUMBNAIL && strcmp(action,"invalidate")==0)) {
        pthread_mutex_lock(&resource->mutex);
        resource->latest[channel]=++resource->token;
        if (resource->active[channel]) __atomic_store_n(&resource->active[channel]->cancelled,true,__ATOMIC_RELAXED);
        radiant_image_job_free(resource->pending[channel]); resource->pending[channel]=nullptr;
        radiant_image_job_free(resource->completed[channel]); resource->completed[channel]=nullptr;
        if (strcmp(action,"invalidate")==0) { resource->cache_epoch++; radiant_thumbnail_clear(resource); }
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
    if (channel==IMAGE_JOB_THUMBNAIL) {
        radiant_rooted_obj_set(output,"cache_hit",{.item=b2it(job->cache_hit)});
        error.set(radiant_obj_new());
        radiant_rooted_obj_set(error,"status",radiant_string_item(image_status_name(job->error.code)));
        radiant_rooted_obj_set(error,"message",radiant_string_item(job->error.message));
        radiant_rooted_obj_set(output,"error",error.get());
        pthread_mutex_lock(&resource->mutex);
        job->result.cache_bytes=resource->thumbnail_bytes;
        pthread_mutex_unlock(&resource->mutex);
    }
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
