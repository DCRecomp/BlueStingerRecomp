#include "test.h"

#include "render/water.h"
#include "render/vulkan/vk_device.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace nr = bs::native::render;

namespace {
template <class T> bool same_bytes(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
}

BS_TEST(water_only_identified_surface_is_animated) {
    BS_CHECK(nr::is_water_surface({32, 32, 0x7458e3539d31cd8dull}));
    BS_CHECK(!nr::is_water_surface({32, 32, 0x7458e3539d31cd8cull}));
    BS_CHECK(!nr::is_water_surface({64, 64, 0x7458e3539d31cd8dull}));

    nr::CommandList cl;
    cl.vertices = {{0, 0, 1, 2, 3}, {1, 0, 1, 4, 5}, {0, 1, 1, 6, 7},
                   {2, 0, 1, 8, 9}, {3, 0, 1, 10, 11}, {2, 1, 1, 12, 13}};
    nr::Draw water, wall;
    water.state.texture.value = 1, water.first_vertex = 0, water.vertex_count = 3;
    wall.state.texture.value = 2, wall.first_vertex = 3, wall.vertex_count = 3;
    cl.draws = {water, wall};
    const auto original = cl;
    const uint8_t tags[] = {1, 0};
    BS_CHECK(nr::animate_water(cl, tags, 17, nr::WaterQuality::Enhanced) == 1);
    for (size_t i = 0; i < 3; ++i) {
        BS_CHECK(cl.vertices[i].x == original.vertices[i].x && cl.vertices[i].y == original.vertices[i].y &&
                 cl.vertices[i].inv_w == original.vertices[i].inv_w);
        BS_CHECK(cl.vertices[i].u != original.vertices[i].u || cl.vertices[i].v != original.vertices[i].v);
    }
    for (size_t i = 3; i < 6; ++i) BS_CHECK(same_bytes(cl.vertices[i], original.vertices[i]));
    BS_CHECK(nr::marked_water_quality(cl.draws[0].state.texture) == nr::WaterQuality::Enhanced);
    BS_CHECK(nr::unmark_water_texture(cl.draws[0].state.texture).value == 1);
    BS_CHECK(same_bytes(cl.draws[1], original.draws[1]));
    BS_CHECK(same_bytes(cl.frame, original.frame));

    nr::CommandList off = original;
    BS_CHECK(nr::animate_water(off, tags, 17, nr::WaterQuality::Original) == 0);
    for (size_t i = 0; i < off.vertices.size(); ++i) BS_CHECK(same_bytes(off.vertices[i], original.vertices[i]));
}

BS_TEST(water_rejects_bad_texture_and_vertex_ranges) {
    nr::CommandList cl;
    cl.vertices.resize(3);
    nr::Draw d;
    d.state.texture.value = 2, d.first_vertex = 2, d.vertex_count = 2;
    cl.draws.push_back(d);
    const uint8_t tags[] = {1};
    BS_CHECK(nr::animate_water(cl, tags, 1, nr::WaterQuality::High) == 0);
}

BS_TEST(water_vulkan_reflects_only_inside_the_marked_surface) {
    std::string error;
    {
        auto dev = nr::VulkanDevice::create(&error);
        if (!dev) BS_SKIP("dispositivo Vulkan");
        const uint32_t texel = 0xB0509CD0u;
        nr::TextureDesc td;
        td.width = td.height = 1;
        auto texture = dev->create_texture(td, std::as_bytes(std::span<const uint32_t>(&texel, 1)));
        BS_CHECK(texture.ok());
        if (!texture) return;

        nr::CommandList cl;
        cl.frame.width = cl.frame.height = 32;
        cl.frame.clear_color = 0xFF79A6C4u;
        cl.frame.translucent_autosort = false;
        cl.vertices = {
            {0, 0, 1, 0, 0, 0xFF24313A}, {32, 0, 1, 1, 0, 0xFF8EAAA9},
            {0, 12, 1, 0, 1, 0xFF354A52}, {32, 12, 1, 1, 1, 0xFFC4D0C7},
            {0, 12, 1, 0, 0, 0xC0FFFFFF}, {32, 12, 1, 4, 0, 0xC0FFFFFF},
            {0, 32, 1, 0, 4, 0xC0FFFFFF}, {32, 32, 1, 4, 4, 0xC0FFFFFF},
        };
        nr::Draw solid, water;
        solid.first_vertex = 0, solid.vertex_count = 4;
        solid.state.depth = nr::DepthTest::Always;
        water.first_vertex = 4, water.vertex_count = 4;
        water.state.list = nr::ListType::Translucent;
        water.state.texture = texture.value();
        water.state.src = nr::Blend::SrcAlpha, water.state.dst = nr::Blend::InvSrcAlpha;
        water.state.depth = nr::DepthTest::Always, water.state.depth_write = false;
        water.state.env = nr::TexEnv::ModulateAlpha, water.state.use_alpha = true;
        cl.draws = {solid, water};

        std::vector<uint32_t> original, enhanced;
        uint32_t w = 0, h = 0;
        BS_CHECK(dev->submit(cl).ok() && dev->read_frame(original, w, h).ok() && w == 32 && h == 32);
        cl.draws[1].state.texture = nr::mark_water_texture(texture.value(), nr::WaterQuality::High);
        BS_CHECK(dev->submit(cl).ok() && dev->read_frame(enhanced, w, h).ok());
        size_t changedWater = 0, changedAbove = 0;
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 32; ++x)
                if (original[size_t(y) * 32 + x] != enhanced[size_t(y) * 32 + x])
                    (y >= 12 ? changedWater : changedAbove)++;
        BS_CHECK(changedWater > 200);
        BS_CHECK(changedAbove == 0);
        dev->destroy_texture(texture.value());
    }
    BS_CHECK(nr::vulkan_live_objects() == 0);
}
