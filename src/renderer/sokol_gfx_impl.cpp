#if defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif

#define SOKOL_GFX_IMPL
#include "sokol_gfx.h"

#define SOKOL_LOG_IMPL
#include "sokol_log.h"
