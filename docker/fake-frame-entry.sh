#!/bin/bash
# Boot the fake Frame: X server, OpenXR runtime, "Steam", sshd.
set -e
U=steamos; H=/home/$U; RT=/run/user/$(id -u $U)
mkdir -p $RT && chown $U $RT && chmod 700 $RT
su $U -c "Xvfb :0 -screen 0 1920x1080x24 >/tmp/xvfb.log 2>&1 &"
sleep 1
# Monado as the "SteamVR" of this box; the active runtime JSON goes where
# SteamVR puts it on a real device (~/.config/openxr/1/active_runtime.json).
su $U -c "mkdir -p $H/.config/openxr/1 && ln -sf /usr/share/openxr/1/openxr_monado.json $H/.config/openxr/1/active_runtime.json"
su $U -c "cd $H && export DISPLAY=:0 XDG_RUNTIME_DIR=$RT; (sleep infinity | SIMULATED_ENABLE=1 XRT_COMPOSITOR_FORCE_XCB=1 monado-service > /tmp/monado.log 2>&1 &)"
su $U -c "cd $H && DISPLAY=:0 XDG_RUNTIME_DIR=$RT nohup python3 /usr/local/bin/fake-steam.py > /tmp/fake-steam.log 2>&1 &"
exec /usr/sbin/sshd -D -e
