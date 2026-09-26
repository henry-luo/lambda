#include "../radiant/render.hpp"

RdtPath* rdt_path_clone(const RdtPath* src) {
    (void)src;
    return nullptr;
}

void rdt_path_free(RdtPath* path) {
    (void)path;
}

RdtPicture* rdt_picture_dup(RdtPicture* pic) {
    (void)pic;
    return nullptr;
}

void rdt_picture_free(RdtPicture* pic) {
    (void)pic;
}

// test pictures are neither SVG documents nor ThorVG text
Element* rdt_picture_get_svg_root(RdtPicture* pic) {
    (void)pic;
    return nullptr;
}

bool rdt_picture_is_text(RdtPicture* pic) {
    (void)pic;
    return false;
}
