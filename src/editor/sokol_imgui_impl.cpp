#if defined(__APPLE__)
#if !defined(SOKOL_METAL)
#define SOKOL_METAL
#endif
#else
#if !defined(SOKOL_GLCORE)
#define SOKOL_GLCORE
#endif
#endif

#include "sokol_gfx.h"
#include "imgui.h"

#define SOKOL_IMGUI_NO_SOKOL_APP
#define SOKOL_IMGUI_IMPL
#include "sokol_imgui.h"
