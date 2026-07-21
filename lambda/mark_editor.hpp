#pragma once

// Transitional IO forwarding header. The authoritative MarkEditor surface is
// io-owned; callers migrate to io/mark_editor.hpp as their dependencies are
// split, without duplicating declarations.
#include "io/mark_editor.hpp"
