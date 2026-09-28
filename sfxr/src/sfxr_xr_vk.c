// sfxr_xr_vk.c - XR_KHR_vulkan_enable2 backend with GL interop.
//
// For runtimes that only accept Vulkan swapchains. raylib still renders with
// OpenGL, into a texture whose memory is owned by Vulkan and shared through
// GL_EXT_memory_object_fd. After rendering we glFinish(), then a tiny Vulkan
// command buffer blits (and vertically flips: GL is bottom-up, Vulkan
// top-down) that shared image into the runtime's swapchain image.
//
// Requirements: GL_EXT_memory_object_fd, and GL + Vulkan on the SAME physical
// device (checked via device UUIDs). True for Mesa (Freedreno/Zink + Turnip on
// the Frame, llvmpipe + lavapipe in tests) and NVIDIA.
//
// Possible upgrades: GL_EXT_semaphore_fd instead of glFinish(); a small ring
// of shared images to overlap GL and Vulkan work.

#include "sfxr_internal.h"
#include "sfxr_gl.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <dlfcn.h>
#include <string.h>

void sfxr_texture_skip_srgb_decode(unsigned tex);

#define MAX_IMAGES 8

#define VKFN(name) static PFN_##name name
VKFN(vkGetInstanceProcAddr);
VKFN(vkDestroyInstance);
VKFN(vkGetPhysicalDeviceProperties2);
VKFN(vkGetPhysicalDeviceQueueFamilyProperties);
VKFN(vkGetPhysicalDeviceMemoryProperties);
VKFN(vkGetDeviceProcAddr);
VKFN(vkDestroyDevice);
VKFN(vkGetDeviceQueue);
VKFN(vkCreateImage);
VKFN(vkDestroyImage);
VKFN(vkGetImageMemoryRequirements);
VKFN(vkAllocateMemory);
VKFN(vkFreeMemory);
VKFN(vkBindImageMemory);
VKFN(vkGetMemoryFdKHR);
VKFN(vkCreateCommandPool);
VKFN(vkDestroyCommandPool);
VKFN(vkAllocateCommandBuffers);
VKFN(vkBeginCommandBuffer);
VKFN(vkEndCommandBuffer);
VKFN(vkResetCommandBuffer);
VKFN(vkCmdPipelineBarrier);
VKFN(vkCmdBlitImage);
VKFN(vkQueueSubmit);
VKFN(vkCreateFence);
VKFN(vkDestroyFence);
VKFN(vkWaitForFences);
VKFN(vkResetFences);
VKFN(vkDeviceWaitIdle);

static struct {
    void *lib;
    XrGraphicsBindingVulkan2KHR binding;
    VkInstance instance;
    VkPhysicalDevice phys;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;

    // shared (GL-rendered) image
    VkImage shared_image;
    VkDeviceMemory shared_mem;
    unsigned gl_mem, gl_tex, gl_fbo, gl_depth_rb;
    int width, height;

    XrSwapchainImageVulkan2KHR images[MAX_IMAGES];
    uint32_t count;
} V;

#define VK_OK(call) vk_ok((call), #call)
static bool vk_ok(VkResult r, const char *what)
{
    if (r == VK_SUCCESS) return true;
    SFXR_WARN("%s failed: VkResult %d", what, (int)r);
    return false;
}

static bool load_instance_fns(void)
{
#define LI(name) name = (PFN_##name)vkGetInstanceProcAddr(V.instance, #name); if (!name) { SFXR_WARN("missing " #name); return false; }
    LI(vkDestroyInstance);
    LI(vkGetPhysicalDeviceProperties2);
    LI(vkGetPhysicalDeviceQueueFamilyProperties);
    LI(vkGetPhysicalDeviceMemoryProperties);
    LI(vkGetDeviceProcAddr);
#undef LI
    return true;
}

static bool load_device_fns(void)
{
#define LD(name) name = (PFN_##name)vkGetDeviceProcAddr(V.device, #name); if (!name) { SFXR_WARN("missing " #name); return false; }
    LD(vkDestroyDevice); LD(vkGetDeviceQueue); LD(vkCreateImage); LD(vkDestroyImage);
    LD(vkGetImageMemoryRequirements); LD(vkAllocateMemory); LD(vkFreeMemory); LD(vkBindImageMemory);
    LD(vkGetMemoryFdKHR); LD(vkCreateCommandPool); LD(vkDestroyCommandPool); LD(vkAllocateCommandBuffers);
    LD(vkBeginCommandBuffer); LD(vkEndCommandBuffer); LD(vkResetCommandBuffer); LD(vkCmdPipelineBarrier);
    LD(vkCmdBlitImage); LD(vkQueueSubmit); LD(vkCreateFence); LD(vkDestroyFence); LD(vkWaitForFences);
    LD(vkResetFences); LD(vkDeviceWaitIdle);
#undef LD
    return true;
}

static bool vk_create_binding(void *instance, uint64_t system, const void **binding)
{
    memset(&V, 0, sizeof(V));
    XrInstance xi = (XrInstance)instance;
    XrSystemId sys = (XrSystemId)system;

    if (!sgl.ImportMemoryFdEXT || !sgl.CreateMemoryObjectsEXT || !sgl.TexStorageMem2DEXT ||
        !sfxr_gl_has_extension("GL_EXT_memory_object_fd")) {
        SFXR_WARN("VK backend needs GL_EXT_memory_object_fd, which this GL driver lacks");
        return false;
    }

    V.lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!V.lib) { SFXR_WARN("cannot load libvulkan.so.1"); return false; }
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(V.lib, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) return false;

    PFN_xrGetVulkanGraphicsRequirements2KHR get_req = NULL;
    PFN_xrCreateVulkanInstanceKHR create_inst = NULL;
    PFN_xrGetVulkanGraphicsDevice2KHR get_dev = NULL;
    PFN_xrCreateVulkanDeviceKHR create_dev = NULL;
    xrGetInstanceProcAddr(xi, "xrGetVulkanGraphicsRequirements2KHR", (PFN_xrVoidFunction *)&get_req);
    xrGetInstanceProcAddr(xi, "xrCreateVulkanInstanceKHR", (PFN_xrVoidFunction *)&create_inst);
    xrGetInstanceProcAddr(xi, "xrGetVulkanGraphicsDevice2KHR", (PFN_xrVoidFunction *)&get_dev);
    xrGetInstanceProcAddr(xi, "xrCreateVulkanDeviceKHR", (PFN_xrVoidFunction *)&create_dev);
    if (!get_req || !create_inst || !get_dev || !create_dev) return false;

    XrGraphicsRequirementsVulkan2KHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
    if (XR_FAILED(get_req(xi, sys, &req))) return false;

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = sfxr_state.cfg.app_name;
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    XrVulkanInstanceCreateInfoKHR xci = { XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
    xci.systemId = sys;
    xci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xci.vulkanCreateInfo = &ici;
    VkResult vr = VK_SUCCESS;
    if (XR_FAILED(create_inst(xi, &xci, &V.instance, &vr)) || vr != VK_SUCCESS) {
        SFXR_WARN("xrCreateVulkanInstanceKHR failed (vk %d)", (int)vr);
        return false;
    }
    if (!load_instance_fns()) return false;

    XrVulkanGraphicsDeviceGetInfoKHR gi = { XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
    gi.systemId = sys;
    gi.vulkanInstance = V.instance;
    if (XR_FAILED(get_dev(xi, &gi, &V.phys))) return false;

    // GL and Vulkan must be the same physical device for memory sharing.
    VkPhysicalDeviceIDProperties idp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    p2.pNext = &idp;
    vkGetPhysicalDeviceProperties2(V.phys, &p2);
    SFXR_LOG("Vulkan device: %s", p2.properties.deviceName);
    if (sgl.GetUnsignedBytevEXT) {
        sgl_ubyte gl_uuid[SGL_UUID_SIZE_EXT] = {0};
        sgl.GetUnsignedBytevEXT(SGL_DEVICE_UUID_EXT, gl_uuid);
        if (memcmp(gl_uuid, idp.deviceUUID, VK_UUID_SIZE) != 0) {
            SFXR_WARN("GL and Vulkan are on different devices (UUID mismatch); interop impossible");
            return false;
        }
    }

    uint32_t nq = 0;
    VkQueueFamilyProperties qf[16];
    vkGetPhysicalDeviceQueueFamilyProperties(V.phys, &nq, NULL);
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(V.phys, &nq, qf);
    V.queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < nq; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { V.queue_family = i; break; }
    if (V.queue_family == UINT32_MAX) return false;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = V.queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char *dev_exts[] = { VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dev_exts;
    XrVulkanDeviceCreateInfoKHR xdci = { XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
    xdci.systemId = sys;
    xdci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xdci.vulkanPhysicalDevice = V.phys;
    xdci.vulkanCreateInfo = &dci;
    if (XR_FAILED(create_dev(xi, &xdci, &V.device, &vr)) || vr != VK_SUCCESS) {
        SFXR_WARN("xrCreateVulkanDeviceKHR failed (vk %d)", (int)vr);
        return false;
    }
    if (!load_device_fns()) return false;
    vkGetDeviceQueue(V.device, V.queue_family, 0, &V.queue);

    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = V.queue_family;
    if (!VK_OK(vkCreateCommandPool(V.device, &pci, NULL, &V.pool))) return false;
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = V.pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (!VK_OK(vkAllocateCommandBuffers(V.device, &cai, &V.cmd))) return false;
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (!VK_OK(vkCreateFence(V.device, &fci, NULL, &V.fence))) return false;

    V.binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
    V.binding.instance = V.instance;
    V.binding.physicalDevice = V.phys;
    V.binding.device = V.device;
    V.binding.queueFamilyIndex = V.queue_family;
    V.binding.queueIndex = 0;
    *binding = &V.binding;
    return true;
}

static int64_t vk_choose_format(const int64_t *formats, uint32_t count)
{
    const int64_t prefs[] = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB };
    for (size_t p = 0; p < sizeof prefs / sizeof prefs[0]; p++)
        for (uint32_t i = 0; i < count; i++)
            if (formats[i] == prefs[p]) return formats[i];
    return 0;
}

static uint32_t find_memory_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(V.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static void submit_and_wait(void)
{
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &V.cmd;
    VK_OK(vkQueueSubmit(V.queue, 1, &si, V.fence));
    vkWaitForFences(V.device, 1, &V.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(V.device, 1, &V.fence);
}

static void image_barrier(VkImage img, VkImageLayout from, VkImageLayout to,
                          uint32_t src_q, uint32_t dst_q, VkAccessFlags src_a, VkAccessFlags dst_a)
{
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = src_q;
    b.dstQueueFamilyIndex = dst_q;
    b.srcAccessMask = src_a;
    b.dstAccessMask = dst_a;
    b.image = img;
    b.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(V.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
}

static bool create_shared_image(int w, int h)
{
    VkExternalMemoryImageCreateInfo emi = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    emi.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.pNext = &emi;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_SRGB;
    ici.extent = (VkExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!VK_OK(vkCreateImage(V.device, &ici, NULL, &V.shared_image))) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(V.device, V.shared_image, &mr);
    VkMemoryDedicatedAllocateInfo ded = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    ded.image = V.shared_image;
    VkExportMemoryAllocateInfo exp = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    exp.pNext = &ded;
    exp.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.pNext = &exp;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX) return false;
    if (!VK_OK(vkAllocateMemory(V.device, &mai, NULL, &V.shared_mem))) return false;
    if (!VK_OK(vkBindImageMemory(V.device, V.shared_image, V.shared_mem, 0))) return false;

    VkMemoryGetFdInfoKHR gfi = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    gfi.memory = V.shared_mem;
    gfi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    if (!VK_OK(vkGetMemoryFdKHR(V.device, &gfi, &fd))) return false;

    // GL side: import the memory (GL takes ownership of fd) and alias a texture.
    sgl.CreateMemoryObjectsEXT(1, &V.gl_mem);
    sgl_int dedicated = 1;
    sgl.MemoryObjectParameterivEXT(V.gl_mem, SGL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
    sgl.ImportMemoryFdEXT(V.gl_mem, mr.size, SGL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);
    sgl.GenTextures(1, &V.gl_tex);
    sgl.BindTexture(SGL_TEXTURE_2D, V.gl_tex);
    sgl.TexParameteri(SGL_TEXTURE_2D, SGL_TEXTURE_TILING_EXT, SGL_OPTIMAL_TILING_EXT);
    sgl.TexStorageMem2DEXT(SGL_TEXTURE_2D, 1, SGL_SRGB8_ALPHA8, w, h, V.gl_mem, 0);
    sgl.BindTexture(SGL_TEXTURE_2D, 0);
    sfxr_texture_skip_srgb_decode(V.gl_tex);

    sgl.GenRenderbuffers(1, &V.gl_depth_rb);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, V.gl_depth_rb);
    sgl.RenderbufferStorage(SGL_RENDERBUFFER, SGL_DEPTH_COMPONENT24, w, h);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, 0);
    V.gl_fbo = sfxr_make_fbo(V.gl_tex, V.gl_depth_rb);
    if (!V.gl_fbo) return false;

    // One-time: UNDEFINED -> GENERAL, then hand it to the "external" (GL) side.
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(V.cmd, &bi);
    image_barrier(V.shared_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                  VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 0, 0);
    vkEndCommandBuffer(V.cmd);
    submit_and_wait();
    return true;
}

static bool vk_setup_images(void *swapchain, int width, int height)
{
    uint32_t n = 0;
    xrEnumerateSwapchainImages((XrSwapchain)swapchain, 0, &n, NULL);
    if (n == 0 || n > MAX_IMAGES) return false;
    for (uint32_t i = 0; i < n; i++) V.images[i] = (XrSwapchainImageVulkan2KHR){ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
    if (XR_FAILED(xrEnumerateSwapchainImages((XrSwapchain)swapchain, n, &n, (XrSwapchainImageBaseHeader *)V.images)))
        return false;
    V.count = n;
    V.width = width;
    V.height = height;
    if (!create_shared_image(width, height)) return false;
    SFXR_LOG("VK swapchain: %u images, shared GL texture %u", n, V.gl_tex);
    return true;
}

static bool vk_image_target(uint32_t index, unsigned *fbo, unsigned *tex)
{
    if (index >= V.count) return false;
    *fbo = V.gl_fbo;
    *tex = V.gl_tex;
    return true;
}

static void vk_image_rendered(uint32_t index)
{
    sgl.Finish();   // GL writes to the shared image are complete

    VkImage dst = V.images[index].image;
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(V.cmd, 0);
    vkBeginCommandBuffer(V.cmd, &bi);

    image_barrier(V.shared_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                  VK_QUEUE_FAMILY_EXTERNAL, V.queue_family, 0, VK_ACCESS_TRANSFER_READ_BIT);
    image_barrier(dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

    VkImageBlit blit = {0};
    blit.srcSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstSubresource = blit.srcSubresource;
    blit.srcOffsets[0] = (VkOffset3D){ 0, 0, 0 };
    blit.srcOffsets[1] = (VkOffset3D){ V.width, V.height, 1 };
    blit.dstOffsets[0] = (VkOffset3D){ 0, V.height, 0 };   // vertical flip
    blit.dstOffsets[1] = (VkOffset3D){ V.width, 0, 1 };
    vkCmdBlitImage(V.cmd, V.shared_image, VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);

    image_barrier(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(V.shared_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                  V.queue_family, VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_READ_BIT, 0);
    vkEndCommandBuffer(V.cmd);
    submit_and_wait();
}

static void vk_destroy(void)
{
    if (V.device) vkDeviceWaitIdle(V.device);
    if (V.gl_fbo) sgl.DeleteFramebuffers(1, &V.gl_fbo);
    if (V.gl_depth_rb) sgl.DeleteRenderbuffers(1, &V.gl_depth_rb);
    if (V.gl_tex) sgl.DeleteTextures(1, &V.gl_tex);
    if (V.gl_mem && sgl.DeleteMemoryObjectsEXT) sgl.DeleteMemoryObjectsEXT(1, &V.gl_mem);
    if (V.device) {
        if (V.shared_image) vkDestroyImage(V.device, V.shared_image, NULL);
        if (V.shared_mem) vkFreeMemory(V.device, V.shared_mem, NULL);
        if (V.fence) vkDestroyFence(V.device, V.fence, NULL);
        if (V.pool) vkDestroyCommandPool(V.device, V.pool, NULL);
    }
    // The device/instance must outlive the XR session; sfxr_xr_shutdown calls
    // us first, so leave them for process exit rather than racing the runtime.
    memset(&V.images, 0, sizeof(V.images));
    V.count = 0;
}

const SfxrXrGfx sfxr_xr_gfx_vk = {
    "vk", XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
    vk_create_binding, vk_choose_format, vk_setup_images,
    vk_image_target, vk_image_rendered, vk_destroy,
    XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT,
};
