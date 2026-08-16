#include "gpu_readback.hpp"

#import <Metal/Metal.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace marrow::tests {

GpuReadbackResult read_sokol_rgba8_image(
    sg_image image,
    int width,
    int height) {
    GpuReadbackResult result;
    result.width = width;
    result.height = height;
    if (width <= 0 || height <= 0) {
        result.error = "Metal readback requires positive dimensions.";
        return result;
    }

    const sg_mtl_image_info image_info = sg_mtl_query_image_info(image);
    if (image_info.active_slot < 0 ||
        image_info.active_slot >= SG_NUM_INFLIGHT_FRAMES ||
        image_info.tex[image_info.active_slot] == nullptr) {
        result.error = "Sokol did not expose the Metal texture for readback.";
        return result;
    }

    id<MTLTexture> texture =
        (__bridge id<MTLTexture>)image_info.tex[image_info.active_slot];
    id<MTLCommandQueue> command_queue =
        (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
    id<MTLDevice> device = (__bridge id<MTLDevice>)sg_mtl_device();
    if (texture == nil || command_queue == nil || device == nil) {
        result.error = "Metal device, queue, or texture was unavailable for readback.";
        return result;
    }

    const std::size_t tight_row_bytes = static_cast<std::size_t>(width) * 4U;
    const std::size_t aligned_row_bytes = (tight_row_bytes + 255U) & ~std::size_t{255U};
    id<MTLBuffer> buffer = [device
        newBufferWithLength:aligned_row_bytes * static_cast<std::size_t>(height)
        options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command_buffer = [command_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
    if (buffer == nil || command_buffer == nil || blit == nil) {
        result.error = "Failed to allocate Metal readback resources.";
        return result;
    }

    [blit copyFromTexture:texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(width, height, 1)
                 toBuffer:buffer
        destinationOffset:0
   destinationBytesPerRow:aligned_row_bytes
 destinationBytesPerImage:aligned_row_bytes * static_cast<std::size_t>(height)];
    [blit endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
    if (command_buffer.status == MTLCommandBufferStatusError) {
        result.error = "Metal readback command failed.";
        return result;
    }

    result.top_left_rgba8.resize(
        tight_row_bytes * static_cast<std::size_t>(height));
    const auto* source = static_cast<const std::uint8_t*>(buffer.contents);
    for (int row = 0; row < height; ++row) {
        std::memcpy(
            result.top_left_rgba8.data() +
                static_cast<std::size_t>(row) * tight_row_bytes,
            source + static_cast<std::size_t>(row) * aligned_row_bytes,
            tight_row_bytes);
    }
    return result;
}

} // namespace marrow::tests
