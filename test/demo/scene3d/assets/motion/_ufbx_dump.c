// A test-only ufbx oracle for the motion fixtures (vibe/Lambda_Pkg_Motion.md
// §10): never linked into lambda.exe. Build it against a pinned ufbx release
// with _oracle.mjs, which writes oracle.json.
//
//   ufbx_dump <file.fbx> <times...>
//
// Prints JSON for every animation stack at each clip-relative time, in metres,
// Y-up, +Z front: node world positions by name, skinned vertices keyed by their
// bind-time position in millimetres, and unskinned mesh vertices in order.
#include "ufbx.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static void print_name(ufbx_string name) {
    putchar('"');
    for (size_t i = 0; i < name.length; i++) {
        char c = name.data[i];
        if (c == '"' || c == '\\') putchar('\\');
        putchar(c);
    }
    putchar('"');
}
static void print_vec(ufbx_vec3 v) { printf("[%.9f,%.9f,%.9f]", v.x, v.y, v.z); }
static long millimetres(double metres) { return lround(metres * 1000.0); }

static void dump_scene(const ufbx_scene* scene, const ufbx_scene* rest) {
    printf("{\"nodes\":{");
    int first = 1;
    for (size_t i = 0; i < scene->nodes.count; i++) {
        const ufbx_node* node = scene->nodes.data[i];
        if (node->is_root || !node->name.length) continue;
        if (!first) putchar(',');
        first = 0;
        print_name(node->name);
        putchar(':');
        print_vec(ufbx_transform_position(&node->node_to_world, (ufbx_vec3){0, 0, 0}));
    }
    printf("},\"skin\":{");
    first = 1;
    for (size_t m = 0; m < scene->meshes.count; m++) {
        const ufbx_mesh* mesh = scene->meshes.data[m];
        if (!mesh->skin_deformers.count) continue;
        const ufbx_skin_deformer* skin = mesh->skin_deformers.data[0];
        const ufbx_node* node = mesh->instances.count ? mesh->instances.data[0] : NULL;
        for (size_t v = 0; v < mesh->vertices.count; v++) {
            ufbx_vec3 local = mesh->vertices.data[v];
            const ufbx_skin_vertex* sv = &skin->vertices.data[v];
            // the converter keeps a vertex's four largest influences; ufbx keeps them all
            if (!sv->num_weights || sv->num_weights > 4) continue;
            // bind-time placement through the vertex's first cluster
            const ufbx_skin_cluster* cluster = skin->clusters.data[skin->weights.data[sv->weight_begin].cluster_index];
            ufbx_matrix bind = ufbx_matrix_mul(&cluster->bind_to_world, &cluster->geometry_to_bone);
            ufbx_vec3 key = ufbx_transform_position(&bind, local);
            ufbx_matrix deform = ufbx_get_skin_vertex_matrix(skin, v, node ? &node->geometry_to_world : NULL);
            if (!first) putchar(',');
            first = 0;
            printf("\"%ld,%ld,%ld\":", millimetres(key.x), millimetres(key.y), millimetres(key.z));
            print_vec(ufbx_transform_position(&deform, local));
        }
    }
    printf("},\"static\":{");
    first = 1;
    for (size_t m = 0; m < scene->meshes.count; m++) {
        const ufbx_mesh* mesh = scene->meshes.data[m];
        if (mesh->skin_deformers.count || !mesh->instances.count) continue;
        const ufbx_node* node = mesh->instances.data[0];
        if (!first) putchar(',');
        first = 0;
        print_name(node->name);
        printf(":[");
        for (size_t v = 0; v < mesh->vertices.count; v++) {
            if (v) putchar(',');
            print_vec(ufbx_transform_position(&node->geometry_to_world, mesh->vertices.data[v]));
        }
        putchar(']');
    }
    printf("}}");
    (void)rest;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: ufbx_dump file.fbx times...\n"); return 2; }
    ufbx_load_opts opts = {0};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(argv[1], &opts, &error);
    if (!scene) { fprintf(stderr, "ufbx: %s\n", error.description.data); return 1; }
    printf("{\"rest\":");
    dump_scene(scene, scene);
    printf(",\"stacks\":{");
    for (size_t s = 0; s < scene->anim_stacks.count; s++) {
        const ufbx_anim_stack* stack = scene->anim_stacks.data[s];
        if (s) putchar(',');
        print_name(stack->name);
        printf(":{\"begin\":%.9f,\"end\":%.9f,\"samples\":{", stack->time_begin, stack->time_end);
        for (int t = 2; t < argc; t++) {
            double seconds = atof(argv[t]);
            ufbx_scene* posed = ufbx_evaluate_scene(scene, stack->anim, stack->time_begin + seconds, NULL, &error);
            if (!posed) { fprintf(stderr, "ufbx evaluate: %s\n", error.description.data); return 1; }
            if (t > 2) putchar(',');
            printf("\"%s\":", argv[t]);
            dump_scene(posed, scene);
            ufbx_free_scene(posed);
        }
        printf("}}");
    }
    printf("}}\n");
    ufbx_free_scene(scene);
    return 0;
}
