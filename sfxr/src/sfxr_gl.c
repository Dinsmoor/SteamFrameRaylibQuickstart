// sfxr_gl.c - runtime loading of the raw GL entry points in sfxr_gl.h.

#include "sfxr_gl.h"
#include "sfxr_internal.h"

#include <string.h>

// raylib compiles GLFW into libraylib (rglfw.c); declare the one function we
// need instead of including GLFW headers.
typedef void (*sfxr_glproc)(void);
extern sfxr_glproc glfwGetProcAddress(const char *procname);

SfxrGL sgl;

#define LOAD(field, name) do { \
        *(void **)(&sgl.field) = (void *)glfwGetProcAddress(name); \
    } while (0)

bool sfxr_gl_load(void)
{
    memset(&sgl, 0, sizeof(sgl));
    LOAD(GenFramebuffers, "glGenFramebuffers");
    LOAD(DeleteFramebuffers, "glDeleteFramebuffers");
    LOAD(BindFramebuffer, "glBindFramebuffer");
    LOAD(FramebufferTexture2D, "glFramebufferTexture2D");
    LOAD(FramebufferRenderbuffer, "glFramebufferRenderbuffer");
    LOAD(CheckFramebufferStatus, "glCheckFramebufferStatus");
    LOAD(GenRenderbuffers, "glGenRenderbuffers");
    LOAD(DeleteRenderbuffers, "glDeleteRenderbuffers");
    LOAD(BindRenderbuffer, "glBindRenderbuffer");
    LOAD(RenderbufferStorage, "glRenderbufferStorage");
    LOAD(RenderbufferStorageMultisample, "glRenderbufferStorageMultisample");
    LOAD(BlitFramebuffer, "glBlitFramebuffer");
    LOAD(GenTextures, "glGenTextures");
    LOAD(DeleteTextures, "glDeleteTextures");
    LOAD(BindTexture, "glBindTexture");
    LOAD(TexParameteri, "glTexParameteri");
    LOAD(TexImage2D, "glTexImage2D");
    LOAD(ReadPixels, "glReadPixels");
    LOAD(Finish, "glFinish");
    LOAD(Flush, "glFlush");
    LOAD(GetIntegerv, "glGetIntegerv");
    LOAD(Disable, "glDisable");
    LOAD(GetString, "glGetString");
    LOAD(GetStringi, "glGetStringi");
    LOAD(CreateMemoryObjectsEXT, "glCreateMemoryObjectsEXT");
    LOAD(DeleteMemoryObjectsEXT, "glDeleteMemoryObjectsEXT");
    LOAD(MemoryObjectParameterivEXT, "glMemoryObjectParameterivEXT");
    LOAD(ImportMemoryFdEXT, "glImportMemoryFdEXT");
    LOAD(TexStorageMem2DEXT, "glTexStorageMem2DEXT");
    LOAD(GetUnsignedBytevEXT, "glGetUnsignedBytevEXT");

    bool core_ok = sgl.GenFramebuffers && sgl.BindFramebuffer && sgl.BlitFramebuffer &&
                   sgl.GenRenderbuffers && sgl.RenderbufferStorageMultisample &&
                   sgl.GetString && sgl.Finish;
    if (!core_ok) SFXR_ERR("missing core GL 3.0 framebuffer entry points");
    return core_ok;
}

bool sfxr_gl_has_extension(const char *name)
{
    if (!sgl.GetIntegerv || !sgl.GetStringi) return false;
    sgl_int n = 0;
    sgl.GetIntegerv(SGL_NUM_EXTENSIONS, &n);
    for (sgl_int i = 0; i < n; i++) {
        const char *e = (const char *)sgl.GetStringi(SGL_EXTENSIONS, (sgl_uint)i);
        if (e && strcmp(e, name) == 0) return true;
    }
    return false;
}

unsigned sfxr_make_fbo(unsigned tex, unsigned depth_rb)
{
    sgl_uint fbo = 0;
    sgl.GenFramebuffers(1, &fbo);
    sgl.BindFramebuffer(SGL_FRAMEBUFFER, fbo);
    sgl.FramebufferTexture2D(SGL_FRAMEBUFFER, SGL_COLOR_ATTACHMENT0, SGL_TEXTURE_2D, tex, 0);
    if (depth_rb) sgl.FramebufferRenderbuffer(SGL_FRAMEBUFFER, SGL_DEPTH_ATTACHMENT, SGL_RENDERBUFFER, depth_rb);
    sgl_enum st = sgl.CheckFramebufferStatus(SGL_FRAMEBUFFER);
    sgl.BindFramebuffer(SGL_FRAMEBUFFER, 0);
    if (st != SGL_FRAMEBUFFER_COMPLETE) {
        SFXR_ERR("framebuffer incomplete (0x%x) for texture %u", st, tex);
        sgl.DeleteFramebuffers(1, &fbo);
        return 0;
    }
    return fbo;
}
