#pragma once

#include "view.hpp"
#include "../lib/ownership.hpp"

inline void radiant_retain_font_family(FontProp* font, lam::PoolPtr<char> family) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(font->family);
    field.set(family);
}

inline void radiant_retain_font_family(FontProp* font, lam::GcPtr<char> family) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(font->family);
    field.set(family);
}

inline void radiant_clear_font_family(FontProp* font) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(font->family);
    field.clear();
}

inline void radiant_retain_background_image(BackgroundProp* background, lam::PoolPtr<char> image) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(background->image);
    field.set(image);
}

inline void radiant_clear_background_image(BackgroundProp* background) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(background->image);
    field.clear();
}

inline void radiant_retain_marker_text_content(MarkerProp* marker, lam::PoolPtr<char> text_content) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(marker->text_content);
    field.set(text_content);
}

inline void radiant_retain_image_source_path(ImageSurface* surface, lam::PoolPtr<char> source_path) {
    lam::PersistentFieldRef<char, lam::PoolDomain> field(surface->source_path);
    field.set(source_path);
}

inline void radiant_take_image_source_path(ImageSurface* surface, lam::SessionPtr<char>& source_path) {
    surface->source_path = lam::detach_session_buffer(source_path);
}

inline void radiant_clear_image_source_path(ImageSurface* surface) {
    surface->source_path = nullptr;
}

inline void radiant_retain_image_source_data(ImageSurface* surface, lam::PoolPtr<unsigned char> source_data, size_t len) {
    lam::PersistentFieldRef<unsigned char, lam::PoolDomain> field(surface->source_data);
    field.set(source_data);
    surface->source_data_len = len;
}

inline void radiant_take_image_source_data(ImageSurface* surface, lam::SessionPtr<unsigned char>& source_data, size_t len) {
    surface->source_data = lam::detach_session_buffer(source_data);
    surface->source_data_len = len;
}

inline void radiant_clear_image_source_data(ImageSurface* surface) {
    surface->source_data = nullptr;
    surface->source_data_len = 0;
}
