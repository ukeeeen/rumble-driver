# Android 手柄震动驱动 · rumble_driver

> 让「系统不支持震动」的蓝牙手柄（GameSir 启明星 / Google Stadia）在任意 root 安卓手机上实现**系统级震动**。零内核模块、零内核编译，纯用户态驱动。

## 它解决什么问题

安卓内核的通用 HID 驱动不映射第三方手柄的厂商震动报告，游戏调用标准震动 API 时系统认为手柄「不可震」（`vibrators=0`）。本驱动以 root 权限运行，架起一座桥：

```
游戏/模拟器（Eden 等，零改动）
    │ 标准震动 API（InputDevice.getVibrator）
    ▼
uinput 虚拟手柄（带 FF_RUMBLE 能力）◄──── 输入事件原样转发 ────┐
    │ FF 效果请求                                             │
    ▼                                                        │
rumble_driver 守护进程（root 用户态）──────────────────────────┘
    │ 翻译成厂商私有报告，hidraw 直写
    ▼
GameSir (report 0x03) / Stadia (report 0x05) → 马达
```

**为什么不需要内核模块 / 不用跟内核版本编译：** 驱动只依赖三个 Linux 稳定接口——`/dev/input/eventX`（evdev）、`/dev/uinput`、`/dev/hidraw`。它们是内核承诺永不改变的 ABI，任何 4.14+ 内核行为一致。缺的从来不是内核「能力」，而是「翻译」这个策略——策略放在用户态跑，就永远不用 insmod、不用配 vermagic、不用 OTA 后重编。已实测跨 **内核 5.10（安卓 17）与 6.1（安卓 16/Flyme）** 双端点。

## 功能

- **系统级震动**：任何 App/游戏/模拟器直接可用，无需逐个适配
- **双手柄并行**：GameSir Nova Lite（蓝牙 X 模式）+ Google Stadia（蓝牙）同时在线
- **原手柄自动隐藏**：`unlink` 节点让系统只见虚拟手柄（= 第一个手柄），照顾「只认第一个手柄」的 App；暂停时 `mknod` 字节级还原
- **意图时间轴**：震动结束时刻 = 锚点 + 累计请求时长。框架队列再怎么拖/批发/倾泻迟到事件，**到点就停**
- **自适应限速**：10–40ms 自动试探蓝牙吞吐上限，EAGAIN 退让 + 5% 振幅噪声门
- **归零兜底**：停止包写失败（EAGAIN）永不放弃，挂起补发直到马达停转
- **开关**：Magisk 管理器「执行」按钮 / 测试 App 按钮 / 标志文件，三路联动，免 root 切换
- **实时状态**：模块简介动态显示「代理了谁 · 隐藏了哪几只 · 是否暂停」
- **热插拔自愈**：3 秒扫描挂载，断线自动重建，守护循环 5 秒重拉

## 安装（需要 root + Android 12+）

1. 从 [Releases](../../releases) 下载模块 zip：
   - `rumble_drivers.zip` — 合集（GameSir + Stadia）
   - `gamesir_rumble.zip` — 仅 GameSir 启明星
   - `stadia_rumble.zip` — 仅 Stadia
2. Magisk / KernelSU / SukiSU 管理器刷入，重启
3. 手柄连接：
   - GameSir：**X + Home** 开机（X 模式，绿灯），蓝牙配对
   - Stadia：直接蓝牙配对
4. 游戏内手柄选择 **GameSir Nova Lite Rumble** / **Stadia Controller Rumble**

买前 10 秒自检（任意手机）：`adb shell ls /dev/hidraw` 有输出即基本可用的信号。

## 仓库结构

```
daemon/
  gamesir_rumble.c    驱动源码（单文件 C，~1000 行）
  build.sh            NDK 编译 + 打包三个模块 zip
  socktest.c          直通 socket 测试客户端
modules/
  combined/  gamesir/  stadia/    三个模块（module.prop / service.sh / action.sh / customize.sh）
app/                  测试 App（Kotlin/Compose）：手柄可视化、驱动开关、时序测试、12 种震动图库
xposed-stubs/         LSPosed 编译桩（可选：钩子直通架构的 stub）
ppt/deck.js           技术分享 PPT 生成脚本
```

## 构建

```bash
# 驱动（需 Android NDK，r27c 测试通过）
cd daemon && bash build.sh

# 测试 App（需 JDK 17 + Android SDK）
./gradlew assembleDebug
```

## 技术要点（踩坑实录）

- **协议**：GameSir X 模式 `[0x03][0x0F][00][00][强0-100][弱0-100]`（xpadneo 社区逆向 + 4 布局脉冲探测实测）；Stadia `[0x05][强u16小端][弱u16小端]`（内核 hid-google-stadiaff.c + 字节级实测）
- **`hidraw_devinfo` 的 VID/PID 是 `__s16`**：PID ≥ 0x8000（如 Stadia 0x9400）读出为负数，比较前必须 `(uint16_t)` 转换
- **节点去重按编号不按 fd**：同一节点重复 open 得新 fd，fd 比较判重会重复挂载
- **防代理套代理**：虚拟手柄与真身同 VID/PID，必须按设备名含 "Rumble" 排除
- **BLE 扛不住 200Hz**：自适应限速 + 流式合并 + 噪声门；停止包 EAGAIN 永不放弃
- **AOSP 震动会话队列**：会批发倾泻/排队拉伸 1.5–2.9×，驱动侧意图时间轴兜底（到点即停）
- **守护循环复活陷阱**：`pkill -x` 杀不死 while 循环的 sh，要按 cmdline 杀

详细时序攻坚过程见 PPT（`ppt/deck.js` 生成）。

## 兼容性边界

| 条件 | 说明 |
|---|---|
| Root 必需 | hidraw/uinput/input 三个节点均为 660 权限，Shizuku（shell 级）实测全被拒 |
| Android 12+ | 手柄震动 API 起点 |
| `/dev/hidraw` | 绝大多数 ROM 满足；个别 ROM 会清零蓝牙 VID/PID（需加名字兜底） |
| 游戏本身要会调震动 API | 驱动只修通路，不创造震动逻辑 |

## License

MIT
