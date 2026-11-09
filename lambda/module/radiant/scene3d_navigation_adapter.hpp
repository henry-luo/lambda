#pragma once

#include "lambda/core/data.hpp"

struct DomElement;
struct Scene3dNavigation;
struct Scene3dNavigationAgent;
Item scene3d_navigation_operate(Scene3dNavigation* state, DomElement* root, Item request);
bool lambda_scene3d_navigation_event(DomElement* target, Scene3dNavigation* state,
                                    Scene3dNavigationAgent* agent, const char* type);
