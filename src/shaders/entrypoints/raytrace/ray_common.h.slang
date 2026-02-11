#ifndef RAY_COMMON_H_SLANG
#define RAY_COMMON_H_SLANG 1

#define RAY_TRACE 1

#include "include/constants.h.slang"

#include "shared/structs.h"      // Defines Material, Light, HitPayload
#include "shared/bindings.h"     // Defines Descriptor Sets, topLevelAS

#include "include/core/math.h.slang"
#include "include/core/rnd.h.slang"
#include "include/core/sampler.h.slang"

#include "include/geometry/vertex_fetch.h.slang" // Uses shared structs & bindings
#include "include/light/lights.h.slang"          // Uses shared structs & math

#include "include/raytrace/trace.h.slang"        // Uses bindings (AS) & structs (Payload)


#endif
