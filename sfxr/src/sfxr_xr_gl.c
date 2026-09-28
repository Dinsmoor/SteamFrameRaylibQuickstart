// sfxr_xr_gl.c - XR_KHR_opengl_enable (Xlib/GLX) swapchain backend.
// raylib renders directly into the runtime's GL swapchain textures.

// X11 typedefs `Font`, which raylib also defines: pull X/GLX/OpenXR platform
// headers in first with X's Font renamed, then raylib via sfxr_internal.h.
#define Font X11Font
#include <X11/Xlib.h>
#include <GL/glx.h>
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#undef Font

#include "sfxr_internal.h"
#include "sfxr_gl.h"

#include <string.h>

void sfxr_texture_skip_srgb_decode(unsigned tex);

#define MAX_IMAGES 8

static struct {
    XrGraphicsBindingOpenGLXlibKHR binding;
    XrSwapchainImageOpenGLKHR images[MAX_IMAGES];
    unsigned fbo[MAX_IMAGES];
    uint32_t count;
    unsigned depth_rb;
    XrSwapchainImageOpenGLKHR depth[MAX_IMAGES];   // depth submission (XR_KHR_composition_layer_depth)
    uint32_t depth_count;
    int attached[MAX_IMAGES];                      // depth image currently on each color FBO (-1: our renderbuffer)
} G;

static bool gl_create_binding(void *instance, uint64_t system, const void **binding)
{
    XrInstance inst = (XrInstance)instance;
    PFN_xrGetOpenGLGraphicsRequirementsKHR get_req = NULL;
    xrGetInstanceProcAddr(inst, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&get_req);
    if (!get_req) return false;
    XrGraphicsRequirementsOpenGLKHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    if (XR_FAILED(get_req(inst, (XrSystemId)system, &req))) return false;  // mandatory call before xrCreateSession

    memset(&G, 0, sizeof(G));
    Display *dpy = glXGetCurrentDisplay();
    GLXContext ctx = glXGetCurrentContext();
    GLXDrawable drw = glXGetCurrentDrawable();
    if (!dpy || !ctx) {
        SFXR_WARN("GL backend needs a current GLX context (is raylib using X11/GLX?)");
        return false;
    }
    int fbid = 0, n = 0;
    glXQueryContext(dpy, ctx, GLX_FBCONFIG_ID, &fbid);
    int attrs[] = { GLX_FBCONFIG_ID, fbid, None };
    GLXFBConfig *cfgs = glXChooseFBConfig(dpy, DefaultScreen(dpy), attrs, &n);
    G.binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
    G.binding.xDisplay = dpy;
    G.binding.glxContext = ctx;
    G.binding.glxDrawable = drw;
    if (cfgs && n > 0) {
        G.binding.glxFBConfig = cfgs[0];
        XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, cfgs[0]);
        if (vi) { G.binding.visualid = (uint32_t)vi->visualid; XFree(vi); }
        XFree(cfgs);
    }
    *binding = &G.binding;
    return true;
}

static int64_t gl_choose_format(const int64_t *formats, uint32_t count)
{
    // We write raylib's (already gamma-space) colors raw; an sRGB swapchain
    // makes the compositor interpret them correctly. See CLAUDE.md "Color".
    const int64_t prefs[] = { SGL_SRGB8_ALPHA8, SGL_RGBA8 };
    for (size_t p = 0; p < sizeof prefs / sizeof prefs[0]; p++)
        for (uint32_t i = 0; i < count; i++)
            if (formats[i] == prefs[p]) return formats[i];
    return count ? formats[0] : 0;
}

static bool gl_setup_images(void *swapchain, int width, int height)
{
    uint32_t n = 0;
    xrEnumerateSwapchainImages((XrSwapchain)swapchain, 0, &n, NULL);
    if (n == 0 || n > MAX_IMAGES) return false;
    for (uint32_t i = 0; i < n; i++) G.images[i] = (XrSwapchainImageOpenGLKHR){ XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR };
    if (XR_FAILED(xrEnumerateSwapchainImages((XrSwapchain)swapchain, n, &n, (XrSwapchainImageBaseHeader *)G.images)))
        return false;
    G.count = n;

    sgl.GenRenderbuffers(1, &G.depth_rb);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, G.depth_rb);
    sgl.RenderbufferStorage(SGL_RENDERBUFFER, SGL_DEPTH_COMPONENT24, width, height);
    sgl.BindRenderbuffer(SGL_RENDERBUFFER, 0);

    for (uint32_t i = 0; i < n; i++) {
        sfxr_texture_skip_srgb_decode(G.images[i].image);
        G.fbo[i] = sfxr_make_fbo(G.images[i].image, G.depth_rb);
        if (!G.fbo[i]) return false;
        G.attached[i] = -1;
    }
    SFXR_LOG("GL swapchain: %u images", n);
    return true;
}

static int64_t gl_choose_depth_format(const int64_t *formats, uint32_t count)
{
    const int64_t prefs[] = { SGL_DEPTH_COMPONENT24, 0x8CAC /* GL_DEPTH_COMPONENT32F */, 0x81A5 /* GL_DEPTH_COMPONENT16 */ };
    for (size_t p = 0; p < sizeof prefs / sizeof prefs[0]; p++)
        for (uint32_t i = 0; i < count; i++)
            if (formats[i] == prefs[p]) return formats[i];
    return 0;
}

static bool gl_setup_depth_images(void *swapchain)
{
    uint32_t n = 0;
    xrEnumerateSwapchainImages((XrSwapchain)swapchain, 0, &n, NULL);
    if (n == 0 || n > MAX_IMAGES) return false;
    for (uint32_t i = 0; i < n; i++) G.depth[i] = (XrSwapchainImageOpenGLKHR){ XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR };
    if (XR_FAILED(xrEnumerateSwapchainImages((XrSwapchain)swapchain, n, &n, (XrSwapchainImageBaseHeader *)G.depth)))
        return false;
    G.depth_count = n;
    return true;
}

// Swap the runtime's depth image in for our renderbuffer on this frame's FBO
// (the two swapchains cycle independently, so the pairing changes).
static void gl_attach_depth(uint32_t color_index, uint32_t depth_index)
{
    if (color_index >= G.count || depth_index >= G.depth_count || G.attached[color_index] == (int)depth_index) return;
    sgl.BindFramebuffer(SGL_FRAMEBUFFER, G.fbo[color_index]);
    sgl.FramebufferTexture2D(SGL_FRAMEBUFFER, SGL_DEPTH_ATTACHMENT, SGL_TEXTURE_2D, G.depth[depth_index].image, 0);
    sgl_enum st = sgl.CheckFramebufferStatus(SGL_FRAMEBUFFER);
    if (st != SGL_FRAMEBUFFER_COMPLETE) {   // fall back to our own depth buffer for this image
        SFXR_WARN("depth image %u doesn't fit framebuffer %u (0x%x); using a private depth buffer", depth_index, color_index, st);
        sgl.FramebufferRenderbuffer(SGL_FRAMEBUFFER, SGL_DEPTH_ATTACHMENT, SGL_RENDERBUFFER, G.depth_rb);
        G.attached[color_index] = -1;
    } else {
        G.attached[color_index] = (int)depth_index;
    }
    sgl.BindFramebuffer(SGL_FRAMEBUFFER, 0);
}

static bool gl_image_target(uint32_t index, unsigned *fbo, unsigned *tex)
{
    if (index >= G.count) return false;
    *fbo = G.fbo[index];
    *tex = G.images[index].image;
    return true;
}

static void gl_image_rendered(uint32_t index) { (void)index; }

static void gl_destroy(void)
{
    for (uint32_t i = 0; i < G.count; i++) if (G.fbo[i]) sgl.DeleteFramebuffers(1, &G.fbo[i]);
    if (G.depth_rb) sgl.DeleteRenderbuffers(1, &G.depth_rb);
    memset(&G, 0, sizeof(G));
}

const SfxrXrGfx sfxr_xr_gfx_gl = {
    "gl", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME,
    gl_create_binding, gl_choose_format, gl_setup_images,
    gl_image_target, gl_image_rendered, gl_destroy,
    0,
    gl_choose_depth_format, gl_setup_depth_images, gl_attach_depth,
};
