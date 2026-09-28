# Headless OpenXR test runtime: Monado (simulated HMD + controllers) on Mesa
# lavapipe/llvmpipe under Xvfb. Lets you exercise the real OpenXR code path
# (session, swapchains, actions, frame loop) with no headset and no GPU.
#
#   make monado-image      # build this image
#   make test-xr           # run an example inside it and screenshot the compositor
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        monado-service libopenxr1-monado libopenxr-loader1 libopenxr-utils \
        mesa-vulkan-drivers libgl1-mesa-dri libglx-mesa0 libegl-mesa0 \
        vulkan-tools mesa-utils xvfb xauth imagemagick xdotool \
        libx11-6 libxrandr2 libxi6 libxcursor1 libxinerama1 libasound2t64 \
        ca-certificates procps \
    && rm -rf /var/lib/apt/lists/*
# Force Mesa software drivers so results match on any host GPU (incl. none).
ENV LIBGL_ALWAYS_SOFTWARE=1 \
    VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
    XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json
