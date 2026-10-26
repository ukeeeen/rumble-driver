#!/system/bin/sh
# GameSir Nova Lite Rumble — 开机自启 + 守护循环（仅GameSir）
MODDIR=${0%/*}
LOG="$MODDIR/daemon.log"

magiskpolicy --live "allow magisk hidraw_device chr_file { open read write getattr ioctl map }" >/dev/null 2>&1
magiskpolicy --live "allow magisk input_device chr_file { open read write getattr ioctl }" >/dev/null 2>&1

chmod 755 "$MODDIR/rumble_driver"
pkill -f "rumble_driver --uinput --only gamesir" >/dev/null 2>&1
sleep 1
echo "=== service.sh started $(date) ===" >> "$LOG"
(
  while true; do
    "$MODDIR/rumble_driver" --uinput --only gamesir >> "$LOG" 2>&1
    echo "--- daemon exited($?), retry in 5s $(date) ---" >> "$LOG"
    sleep 5
  done
) &
