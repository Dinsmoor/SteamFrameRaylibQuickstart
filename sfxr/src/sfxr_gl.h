// sfxr_gl.h - the handful of raw OpenGL entry points sfxr needs beyond rlgl
// (multisample FBOs, blits, and EXT_memory_object for GL<->Vulkan sharing).
// Loaded at runtime through GLFW so we never include a GL loader header that
// could fight with the one compiled into raylib.

#ifndef SFXR_GL_H
#define SFXR_GL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef unsigned int  sgl_enum;
typedef unsigned int  sgl_uint;
typedef int           sgl_int;
typedef int           sgl_sizei;
typedef unsigned char sgl_ubyte;
typedef uint64_t      sgl_uint64;

#define SGL_TEXTURE_2D                 0x0DE1
#define SGL_TEXTURE_MIN_FILTER         0x2801
#define SGL_TEXTURE_MAG_FILTER         0x2800
#define SGL_LINEAR                     0x2601
#define SGL_FRAMEBUFFER                0x8D40
#define SGL_READ_FRAMEBUFFER           0x8CA8
#define SGL_DRAW_FRAMEBUFFER           0x8CA9
#define SGL_RENDERBUFFER               0x8D41
#define SGL_COLOR_ATTACHMENT0          0x8CE0
#define SGL_DEPTH_ATTACHMENT           0x8D00
#define SGL_FRAMEBUFFER_COMPLETE       0x8CD5
#define SGL_COLOR_BUFFER_BIT           0x00004000
#define SGL_NEAREST                    0x2600
#define SGL_SRGB8_ALPHA8               0x8C43
#define SGL_RGBA8                      0x8058
#define SGL_DEPTH_COMPONENT24          0x81A6
#define SGL_MAX_SAMPLES                0x8D57
#define SGL_VENDOR                     0x1F00
#define SGL_RENDERER                   0x1F01
#define SGL_VERSION                    0x1F02
#define SGL_NUM_EXTENSIONS             0x821D
#define SGL_EXTENSIONS                 0x1F03
#define SGL_FRAMEBUFFER_SRGB           0x8DB9
#define SGL_RGBA                       0x1908
#define SGL_UNSIGNED_BYTE              0x1401
// EXT_memory_object / EXT_memory_object_fd
#define SGL_TEXTURE_TILING_EXT         0x9580
#define SGL_OPTIMAL_TILING_EXT         0x9584
#define SGL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#define SGL_HANDLE_TYPE_OPAQUE_FD_EXT  0x9586
#define SGL_DEVICE_UUID_EXT            0x9597
#define SGL_UUID_SIZE_EXT              16

typedef struct {
    void (*GenFramebuffers)(sgl_sizei, sgl_uint *);
    void (*DeleteFramebuffers)(sgl_sizei, const sgl_uint *);
    void (*BindFramebuffer)(sgl_enum, sgl_uint);
    void (*FramebufferTexture2D)(sgl_enum, sgl_enum, sgl_enum, sgl_uint, sgl_int);
    void (*FramebufferRenderbuffer)(sgl_enum, sgl_enum, sgl_enum, sgl_uint);
    sgl_enum (*CheckFramebufferStatus)(sgl_enum);
    void (*GenRenderbuffers)(sgl_sizei, sgl_uint *);
    void (*DeleteRenderbuffers)(sgl_sizei, const sgl_uint *);
    void (*BindRenderbuffer)(sgl_enum, sgl_uint);
    void (*RenderbufferStorage)(sgl_enum, sgl_enum, sgl_sizei, sgl_sizei);
    void (*RenderbufferStorageMultisample)(sgl_enum, sgl_sizei, sgl_enum, sgl_sizei, sgl_sizei);
    void (*BlitFramebuffer)(sgl_int, sgl_int, sgl_int, sgl_int, sgl_int, sgl_int, sgl_int, sgl_int, sgl_uint, sgl_enum);
    void (*GenTextures)(sgl_sizei, sgl_uint *);
    void (*DeleteTextures)(sgl_sizei, const sgl_uint *);
    void (*BindTexture)(sgl_enum, sgl_uint);
    void (*TexParameteri)(sgl_enum, sgl_enum, sgl_int);
    void (*ReadPixels)(sgl_int, sgl_int, sgl_sizei, sgl_sizei, sgl_enum, sgl_enum, void *);
    void (*TexImage2D)(sgl_enum, sgl_int, sgl_int, sgl_sizei, sgl_sizei, sgl_int, sgl_enum, sgl_enum, const void *);
    void (*Finish)(void);
    void (*Flush)(void);
    void (*GetIntegerv)(sgl_enum, sgl_int *);
    void (*Disable)(sgl_enum);
    const sgl_ubyte *(*GetString)(sgl_enum);
    const sgl_ubyte *(*GetStringi)(sgl_enum, sgl_uint);
    // EXT_memory_object(_fd) -- may be NULL
    void (*CreateMemoryObjectsEXT)(sgl_sizei, sgl_uint *);
    void (*DeleteMemoryObjectsEXT)(sgl_sizei, const sgl_uint *);
    void (*MemoryObjectParameterivEXT)(sgl_uint, sgl_enum, const sgl_int *);
    void (*ImportMemoryFdEXT)(sgl_uint, sgl_uint64, sgl_enum, sgl_int);
    void (*TexStorageMem2DEXT)(sgl_enum, sgl_sizei, sgl_enum, sgl_sizei, sgl_sizei, sgl_uint, sgl_uint64);
    void (*GetUnsignedBytevEXT)(sgl_enum, sgl_ubyte *);
} SfxrGL;

extern SfxrGL sgl;

bool sfxr_gl_load(void);                    // after InitWindow
bool sfxr_gl_has_extension(const char *name);

#endif // SFXR_GL_H
