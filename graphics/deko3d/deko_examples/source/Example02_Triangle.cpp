/*
** deko3d Example 02: Triangle
** This example shows how to draw a basic multi-colored triangle.
** New concepts in this example:
** - Loading and using shaders
** - Setting up basic 3D engine state
** - Setting up vertex attributes and vertex buffers
** - Drawing primitives
*/

// Sample Framework headers
#include "SampleFramework/CApplication.h"
#include "SampleFramework/CMemPool.h"
#include "SampleFramework/CShader.h"

// C++ standard library headers
#include <array>
#include <optional>

namespace
{
    struct Vertex
    {
        float position[3];
        float color[3];
    };

    constexpr std::array VertexAttribState =
    {
        DkVtxAttribState{ 0, 0, offsetof(Vertex, position), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0 },
        DkVtxAttribState{ 0, 0, offsetof(Vertex, color),    DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0 },
    };

    constexpr std::array VertexBufferState =
    {
        DkVtxBufferState{ sizeof(Vertex), 0 },
    };

    constexpr std::array TriangleVertexData =
    {
        Vertex{ {  0.0f, +1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        Vertex{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        Vertex{ { +1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    };
}

class CExample02 final : public CApplication
{
    static constexpr unsigned NumFramebuffers = 2;
    static constexpr uint32_t FramebufferWidth = 1280;
    static constexpr uint32_t FramebufferHeight = 720;
    static constexpr unsigned StaticCmdSize = 0x10000;

    PadState pad;

    dk::UniqueDevice device;
    dk::UniqueQueue queue;

    std::optional<CMemPool> pool_images;
    std::optional<CMemPool> pool_code;
    std::optional<CMemPool> pool_data;

    dk::UniqueCmdBuf cmdbuf;

    CShader vertexShader;
    CShader fragmentShader;

    CMemPool::Handle vertexBuffer;

    CMemPool::Handle framebuffers_mem[NumFramebuffers];
    dk::Image framebuffers[NumFramebuffers];
    DkCmdList framebuffer_cmdlists[NumFramebuffers];
    dk::UniqueSwapchain swapchain;

    DkCmdList render_cmdlist;

public:
    CExample02()
    {
        // Create the deko3d device
        device = dk::DeviceMaker{}.create();

        // Create the main queue
        queue = dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics).create();

        // Create the memory pools
        pool_images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16*1024*1024);
        pool_code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);
        pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1*1024*1024);

        // Create the static command buffer and feed it freshly allocated memory
        cmdbuf = dk::CmdBufMaker{device}.create();
        CMemPool::Handle cmdmem = pool_data->allocate(StaticCmdSize);
        cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

        // Load the shaders
        vertexShader.load(*pool_code, "romfs:/shaders/basic_vsh.dksh");
        fragmentShader.load(*pool_code, "romfs:/shaders/color_fsh.dksh");

        // Load the vertex buffer
        vertexBuffer = pool_data->allocate(sizeof(TriangleVertexData), alignof(Vertex));
        memcpy(vertexBuffer.getCpuAddr(), TriangleVertexData.data(), vertexBuffer.getSize());

        // Create the framebuffer resources
        createFramebufferResources();

        // Initialize gamepad
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
    }

    ~CExample02()
    {
        // Destroy the framebuffer resources
        destroyFramebufferResources();

        // Destroy the vertex buffer (not strictly needed in this case)
        vertexBuffer.destroy();
    }

    void createFramebufferResources()
    {
        // Create layout for the framebuffers
        dk::ImageLayout layout_framebuffer;
        dk::ImageLayoutMaker{device}
            .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(FramebufferWidth, FramebufferHeight)
            .initialize(layout_framebuffer);

        // Create the framebuffers
        std::array<DkImage const*, NumFramebuffers> fb_array;
        uint64_t fb_size  = layout_framebuffer.getSize();
        uint32_t fb_align = layout_framebuffer.getAlignment();
        for (unsigned i = 0; i < NumFramebuffers; i ++)
        {
            // Allocate a framebuffer
            framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
            framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

            // Generate a command list that binds it
            dk::ImageView colorTarget{ framebuffers[i] };
            cmdbuf.bindRenderTargets(&colorTarget);
            framebuffer_cmdlists[i] = cmdbuf.finishList();

            // Fill in the array for use later by the swapchain creation code
            fb_array[i] = &framebuffers[i];
        }

        // Create the swapchain using the framebuffers
        swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), fb_array}.create();

        // Generate the main rendering cmdlist
        recordStaticCommands();
    }

    void destroyFramebufferResources()
    {
        // Return early if we have nothing to destroy
        if (!swapchain) return;

        // Make sure the queue is idle before destroying anything
        queue.waitIdle();

        // Clear the static cmdbuf, destroying the static cmdlists in the process
        cmdbuf.clear();

        // Destroy the swapchain
        swapchain.destroy();

        // Destroy the framebuffers
        for (unsigned i = 0; i < NumFramebuffers; i ++)
            framebuffers_mem[i].destroy();
    }

    void recordStaticCommands()
    {
        // Initialize state structs with deko3d defaults
        dk::RasterizerState rasterizerState;
        dk::ColorState colorState;
        dk::ColorWriteState colorWriteState;

        // Configure viewport and scissor
        cmdbuf.setViewports(0, { { 0.0f, 0.0f, FramebufferWidth, FramebufferHeight, 0.0f, 1.0f } });
        cmdbuf.setScissors(0, { { 0, 0, FramebufferWidth, FramebufferHeight } });

        // Clear the color buffer
        cmdbuf.clearColor(0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 0.0f);

        // Bind state required for drawing the triangle
        cmdbuf.bindShaders(DkStageFlag_GraphicsMask, { vertexShader, fragmentShader });
        cmdbuf.bindRasterizerState(rasterizerState);
        cmdbuf.bindColorState(colorState);
        cmdbuf.bindColorWriteState(colorWriteState);
        cmdbuf.bindVtxBuffer(0, vertexBuffer.getGpuAddr(), vertexBuffer.getSize());
        cmdbuf.bindVtxAttribState(VertexAttribState);
        cmdbuf.bindVtxBufferState(VertexBufferState);

        // Draw the triangle
        cmdbuf.draw(DkPrimitive_Triangles, TriangleVertexData.size(), 1, 0, 0);

        // Finish off this command list
        render_cmdlist = cmdbuf.finishList();
    }

    void render()
    {
        // Acquire a framebuffer from the swapchain (and wait for it to be available)
        int slot = queue.acquireImage(swapchain);

        // Run the command list that attaches said framebuffer to the queue
        queue.submitCommands(framebuffer_cmdlists[slot]);

        // Run the main rendering command list
        queue.submitCommands(render_cmdlist);

        // Now that we are done rendering, present it to the screen
        queue.presentImage(swapchain, slot);
    }

    bool onFrame(u64 ns) override
    {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus)
            return false;

        render();
        return true;
    }
};

void Example02(void)
{
    CExample02 app;
    app.run();
}
