# blender.Dockerfile - headless Blender for the asset pipeline (docs/ASSETS.md).
#
# Blender publishes no Linux ARM64 build, and the build machine here is ARM64,
# so Blender comes from Alpine edge, which packages the current release for
# aarch64 (5.2.2 on 2026-09-29). Inside: Blender, the glTF exporter it ships
# with, and Blender Lab's MCP add-on (the socket bridge an LLM talks to
# through the `blender-mcp` server; https://www.blender.org/lab/mcp-server/).
#
#   make blender-image                  build it (scripts/blender.sh builds it on first use too)
#   scripts/blender.sh -b --python x.py run Blender in it, the repo mounted at /w
#   scripts/blender.sh mcp              the MCP bridge on 127.0.0.1:9876 (background mode)
FROM alpine:edge
ARG UID=1000
ARG GID=1000
ARG MCP_ADDON=https://projects.blender.org/lab/blender_mcp/releases/download/v1.0.3/mcp-1.0.3.zip
# (Alpine's blender package doesn't pull in the Python modules Blender's own
# scripts import: numpy for the glTF exporter, requests and cattrs for the
# extensions system; without them the exporter fails and startup logs tracebacks)
RUN apk add --no-cache blender py3-numpy py3-requests py3-cattrs curl unzip bash \
 && addgroup -g $GID blender 2>/dev/null || true \
 && adduser -D -u $UID -G "$(getent group $GID | cut -d: -f1)" -h /home/blender blender
USER blender
ENV HOME=/home/blender
# The MCP add-on: unpacked where a drag-and-drop install would put it, then
# enabled once (with "online access", which its server requires) and the
# preferences saved, so every later run has it.
RUN v=$(blender --version | head -1 | sed -E 's/Blender ([0-9]+\.[0-9]+).*/\1/') \
 && d="$HOME/.config/blender/$v/extensions/user_default/mcp" && mkdir -p "$d" \
 && curl -fsSL -o /tmp/mcp.zip "$MCP_ADDON" && unzip -q /tmp/mcp.zip -d "$d" && rm /tmp/mcp.zip \
 && blender -b --python-expr "import bpy; bpy.context.preferences.system.use_online_access = True; bpy.ops.preferences.addon_enable(module='bl_ext.user_default.mcp'); bpy.ops.wm.save_userpref()" \
 && blender -b --python-expr "import bpy; print('mcp add-on:', 'bl_ext.user_default.mcp' in bpy.context.preferences.addons)"
WORKDIR /w
ENTRYPOINT ["blender"]
