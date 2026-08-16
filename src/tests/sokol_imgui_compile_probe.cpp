#define SOKOL_DUMMY_BACKEND
#define SOKOL_IMGUI_IMPL
#define SOKOL_IMGUI_NO_SOKOL_APP

#include "sokol_gfx.h"
#include "imgui.h"
#include "sokol_imgui.h"

void marrow_sokol_imgui_compile_probe() {
    simgui_desc_t descriptor{};
    descriptor.color_format = SG_PIXELFORMAT_RGBA8;
    descriptor.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    descriptor.sample_count = 1;
}
