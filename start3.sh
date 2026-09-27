echo high | sudo tee /sys/class/drm/card1/device/power_dpm_force_performance_level
XKB_DEFAULT_LAYOUT=it VELA_STATS=1 build/compositor/vela-compositor -s build/shell/vela-shell 2> ~/vela-3.log
echo auto | sudo tee /sys/class/drm/card1/device/power_dpm_force_performance_level

