#!/system/bin/sh
# 管理器「执行」按钮：切换 驱动开启(隐藏原手柄) / 驱动关闭(显示原手柄)
# 与 App 内开关同一套标志文件，≤2秒生效
FLAG1=/sdcard/rumble_driver_off
FLAG2=/sdcard/Android/data/com.ukeeeen.gamepadrumbletest/files/rumble_off

if [ -f "$FLAG1" ] || [ -f "$FLAG2" ]; then
    rm -f "$FLAG1" "$FLAG2"
    echo "✅ 驱动开启：原手柄已隐藏，游戏用虚拟手柄（有震动）"
else
    touch "$FLAG1"
    echo "⛔ 驱动关闭：原手柄恢复直连（无震动）"
fi
sleep 2
exit 0
