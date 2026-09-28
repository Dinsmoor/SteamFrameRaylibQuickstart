# patches/: small fixes applied to vendored raylib

`make` unpacks `external/raylib-<ver>-src.tar.gz` and then applies every
`patches/raylib-<ver>-*.patch` in name order. Editing or adding a patch
re-extracts raylib on the next build, so there is nothing to clean by hand.

Keep this list short. Every patch here should say what broke, how we proved
it, and when it can be deleted.

## raylib-6.0-orphan-batch-buffers.patch

**What broke:** on the Steam Frame, vrui panels (anything drawn into a
`RenderTexture`) came out blank: just the clear color, plus a speck or two in
a corner. The same program was fine on desktop GPUs and on llvmpipe.

**Why:** raylib collects 2D/3D draws into one vertex buffer (the "render
batch"). It uploads that buffer with `glBufferSubData` every time it flushes,
which happens many times a frame. On the Frame, OpenGL runs on top of Vulkan
through Mesa's **Zink** driver. Zink tries to speed things up by moving buffer
uploads earlier in its Vulkan command stream. raylib's pattern is to overwrite
the same buffer, draw, overwrite it again and draw again. Moving the uploads
earlier let the panel's draws read vertices that belonged to *later* draws in
the frame. Those vertices are in world meters, not panel pixels, so the panel
texture got a speck in its corner and nothing else.

**How we proved it:** we replayed a recorded headset session on the Frame's own
GPU (`frame.sh push-recording`, then `frame.sh shot ... SFXR_REPLAY=...`):
- As-is: blank panels.
- Dumping the panel texture right after drawing it, which forces the GPU to
  catch up: the content appeared, but the panel title showed glyphs from text
  drawn later in the same panel.
- With `ZINK_DEBUG=noreorder`, which turns off Zink's upload reordering: correct.
- With this patch and no env var: correct.

**The fix:** before each upload, call `glBufferData(..., NULL, ...)` for the
whole buffer. This is the classic "buffer orphaning" idiom. It tells the
driver we don't need the old contents, so the driver gives the buffer fresh
storage instead of overwriting memory that a queued draw may still read.
Drivers recycle that storage, so this costs little, and it's the pattern
they're built to handle quickly.

**Delete when:** upstream raylib orphans its batch buffers itself, or Zink on
the Frame no longer reorders these uploads. To check, build without the patch
and replay, on the device, a session that shows a panel (as above).
