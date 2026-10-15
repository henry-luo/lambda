#pragma once
#include "radiant_pdf_graphic.hpp"
#include "../../../radiant/layout_paged.hpp"

Item radiant_pdf_metadata(DomDocument*);
Item radiant_pdf_page_properties(const ViewPageBox*);
void radiant_pdf_link(Rooted<Item>&, DomNode*, Rect);
void radiant_pdf_destination(Rooted<Item>&, DomNode*, float, float);
