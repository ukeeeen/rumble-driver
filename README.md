# Android 手柄震动驱动 · rumble_driver

给 GameSir 启明星和 Google Stadia 补上震动。这两只手柄在多数安卓机上按键、摇杆都正常，但系统不暴露震动能力（`vibrators=0`），游戏调用震动 API 没有任何反应。

驱动以 root 权限运行：用 uinput 造一只有震动能力的虚拟手柄转发全部输入，再把游戏发来的震动请求翻译成厂商私有报文，写到 `/dev/hidraw`。

不用编译内核模块，也不用跟着内核版本重新打包。同一份 aarch64 二进制在内核 5.10（Android 17）和 6.1（Android 16 / Flyme）上都实测可用。

## 原理

按键之所以能用，是因为输入事件走的是标准 evdev 路径。震动不行，因为震动需要往手柄写厂商自定义的输出报告，而内核的通用 HID 驱动不认识这个格式，于是整个 FF（力反馈）能力就不会出现在设备上。

驱动只用到三个内核接口：

| 接口 | 用途 |
|---|---|
| `/dev/input/eventX` | 读实体手柄的按键和摇杆 |
| `/dev/uinput` | 创建带 `FF_RUMBLE` 的虚拟手柄 |
| `/dev/hidraw` | 直接写厂商震动报文 |

这三个接口的 ABI 在内核里长期稳定，和版本号无关。把游戏请求翻译成厂商报文这一层，完全可以放在用户态做，所以不需要 insmod，OTA 之后也不会失效。

```
游戏 / 模拟器
    │ 标准震动 API（InputDevice.getVibrator）
    ▼
uinput 虚拟手柄（带 FF_RUMBLE）◄──── 输入事件转发 ────┐
    │ FF 效果请求                                    │
    ▼                                               │
rumble_driver 守护进程（root）────────────────────────┘
    │ 翻译成厂商报文，hidraw 直写
    ▼
GameSir（report 0x03）/ Stadia（report 0x05）→ 马达
```

## 功能

- 系统级震动，任何 App、游戏、模拟器都可直接使用，不需要逐个适配
- GameSir Nova Lite 与 Google Stadia 可同时连接，各自独立代理
- 自动隐藏原手柄节点，系统里只剩虚拟手柄，避免只认第一个手柄的 App 绑到哑设备；暂停时节点按原主次设备号恢复
- 震动结束时刻按累计请求时长计算，框架队列积压时也到点就停
- 发送间隔在 10–40ms 之间自适应，遇到 EAGAIN 退让
- 停止包写失败不会被丢弃，挂起补发直到马达停转
- Magisk 管理器「执行」按钮、测试 App 按钮、标志文件三种方式切换隐藏/直连
- 模块简介实时显示当前代理了几个手柄、隐藏了哪几只、是否暂停
- 每 3 秒扫描一次设备，断线自动重建

## 安装

需要 root（Magisk / KernelSU / SukiSU）和 Android 12 以上。

1. 从 [Releases](../../releases) 下载模块 zip
   - `rumble_drivers.zip` — 合集
   - `gamesir_rumble.zip` — 仅 GameSir 启明星
   - `stadia_rumble.zip` — 仅 Stadia
2. 用 Magisk / KernelSU / SukiSU 管理器刷入，重启
3. 连接手柄
   - GameSir：开机按住 **B + Home**，指示灯变蓝（Xbox/Windows 模式），然后蓝牙配对
   - Stadia：直接蓝牙配对
4. 游戏内选择 **GameSir Nova Lite Rumble** 或 **Stadia Controller Rumble**

刷之前可以先确认机器有没有戏：`adb shell ls /dev/hidraw`，能列出节点就说明有。

## 仓库结构

```
daemon/
  gamesir_rumble.c    驱动源码，单文件 C，1016 行
  build.sh            NDK 编译 + 打包三个模块 zip
  socktest.c          直通 socket 测试客户端
modules/
  combined/  gamesir/  stadia/
                      三个模块的 module.prop / service.sh / action.sh / customize.sh
app/                  测试 App（Kotlin/Compose），含可视化、驱动开关、时序测试、震动图库
xposed-stubs/         LSPosed 编译桩，钩子直通方案使用
```

## 构建

```bash
# 驱动：需要 Android NDK，r27c 验证通过
cd daemon && bash build.sh

# 测试 App：需要 JDK 17 和 Android SDK
./gradlew assembleDebug
```

## 兼容性边界

| 条件 | 说明 |
|---|---|
| Root | 必需。hidraw、uinput、input 三个节点都是 660 权限，Shizuku 的 shell 身份实测全部被拒 |
| Android 12+ | 手柄震动 API 从这个版本开始有 |
| `/dev/hidraw` | 绝大多数 ROM 都有；个别 ROM 会清零蓝牙设备的 VID/PID，这种要按设备名匹配，当前代码没做 |
| 游戏本身会调震动 API | 驱动只修通路，不创造震动逻辑。游戏不发震动请求就没有震动 |

## License

MIT
