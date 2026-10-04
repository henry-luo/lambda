#include "../radiant/render.hpp"

// The GIF and Lottie player tests link the image surface table, whose release
// path frees a surface's picture; their surfaces never carry one.
void rdt_picture_free(RdtPicture* pic) {
    (void)pic;
}
