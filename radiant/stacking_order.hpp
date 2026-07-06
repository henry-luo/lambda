#pragma once

#include "view.hpp"
#include "../lib/arraylist.h"

bool radiant_stack_is_positive_z_positioned(View* view);
bool radiant_stack_is_out_of_flow_positioned(View* view);
bool radiant_stack_is_deferred_from_normal_flow(View* view);

ArrayList* radiant_stack_collect_positive_z_descendants(View* first_child, const char* log_prefix);
ArrayList* radiant_stack_collect_positioned_children(ViewBlock* block, const char* log_prefix);
void radiant_stack_sort_in_paint_order(ArrayList* views);

