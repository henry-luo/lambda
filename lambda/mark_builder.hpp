#pragma once

// Transitional IO forwarding header. The authoritative MarkBuilder surface is
// io-owned; callers migrate to io/mark_builder.hpp as their dependencies are
// split, without duplicating declarations.
#include "io/mark_builder.hpp"
