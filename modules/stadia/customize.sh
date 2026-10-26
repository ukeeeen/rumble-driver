SKIPUNZIP=0
ui_print "- Stadia Controller Rumble Driver 安装中..."
set_perm "$MODDIR/rumble_driver" 0 0 0755
set_perm "$MODDIR/service.sh" 0 0 0755
set_perm "$MODDIR/action.sh" 0 0 0755
ui_print "- 完成。Stadia 手柄直接蓝牙配对，重启后生效。"
ui_print "- 特性：震动 / 原手柄自动隐藏 / 管理器按钮与 App 可暂停直连"
