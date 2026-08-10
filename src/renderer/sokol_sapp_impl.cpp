#define SOKOL_NO_ENTRY

#if defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif

#define SOKOL_APP_IMPL
#include "sokol_app.h"

#include "sokol_gfx.h"

#define SOKOL_GLUE_IMPL
#include "sokol_glue.h"
