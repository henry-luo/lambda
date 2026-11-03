#pragma once

// one implementation in audio_bus.cpp supplies both device and memory-decoder APIs.
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#include "../lib/miniaudio/miniaudio.h"
