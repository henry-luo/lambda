#pragma once
#include "../core/navigation_graph.hpp"
Item navigation_path_value(const NavigationCompiled& compiled,Item start,Item goal,Item options);
Item navigation_point_value(NavigationVec point,unsigned dimensions);
Item navigation_settings_value(const NavigationBakeSettings& settings);
Item navigation_bake_value(const NavigationBake* bake,const char* source_hash,const char* settings_hash,const NavigationCompiled* definitions=nullptr);
