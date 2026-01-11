#ifndef RADIANT_GRAPH_TO_SVG_HPP
#define RADIANT_GRAPH_TO_SVG_HPP

#include "graph_layout_types.hpp"
#include "../lambda/lambda-data.hpp"

// Generate SVG element tree from laid-out graph
Item graph_to_svg(Element* graph, GraphLayout* layout, Input* input);

// Generate SVG with custom styling options
Item graph_to_svg_with_options(Element* graph, GraphLayout* layout, 
                               SvgGeneratorOptions* opts, Input* input);

// Get default SVG generator options
SvgGeneratorOptions* create_default_svg_options();

#endif // RADIANT_GRAPH_TO_SVG_HPP
