SKIPUNZIP=0
ui_print "- Controller Rumble Drivers (GameSir + Stadia) 安装中..."
set_perm "$MODDIR/rumble_driver" 0 0 0755
set_perm "$MODDIR/service.sh" 0 0 0755
ui_print "- 完成。GameSir 用 X 模式连接，Stadia 直接蓝牙配对，重启后生效。"
ui_print "- 特性：双手柄震动 / 原手柄自动隐藏 / App 内可暂停直连"
