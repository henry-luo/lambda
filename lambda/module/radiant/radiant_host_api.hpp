#pragma once

#include "../../jube/jube.h"

extern const JubeHostAPI* radiant_host_api;

#ifdef __APPLE__
extern "C" Item radiant_lambda_fn_call3_into(Function* fn, Item a, Item b, Item c,
                                               uint64_t* result_home) asm("_fn_call3_into");
#else
extern "C" Item radiant_lambda_fn_call3_into(Function* fn, Item a, Item b, Item c,
                                               uint64_t* result_home) asm("fn_call3_into");
#endif

extern "C" Item fn_radiant_pdf_walk(Item path, Item options, Item callback, Item schemas);
extern "C" Item fn_radiant_pdf_walk_source(Item source, Item options, Item callback, Item schemas);
extern "C" Item fn_radiant_pdf_glyph_path(Item context);
extern "C" Item fn_radiant_pdf_clip_path(Item context);
extern "C" Item fn_radiant_pdf_image(Item context);
extern "C" Item fn_radiant_pdf_object(Item dictionary);
extern "C" Item fn_radiant_pdf_deflate(Item bytes);
extern "C" Item fn_radiant_pdf_measure(Item content);
