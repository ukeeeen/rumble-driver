/* 魅族21 手柄震动驱动 · 技术分享 PPT */
const pptxgen = require("pptxgenjs");

const p = new pptxgen();
p.layout = "LAYOUT_WIDE";           // 13.33 × 7.5
p.author = "ukeeeen";
p.title = "魅族21 手柄震动驱动 · 从系统不支持到自制驱动";

/* ===== 调色板：底层系统风（深板岩 + 青绿 + 琥珀） ===== */
const BG_DARK  = "0F172A";   // 封面/结尾
const BG       = "F8FAFC";   // 内容页背景
const PRIMARY  = "0D9488";   // teal 600 主色
const PRIM_D   = "115E59";   // teal 800 深色变体
const PRIM_T   = "CCFBF1";   // teal 极浅底
const ACCENT   = "D97706";   // amber 强调
const TEXT     = "1E293B";
const MUTED    = "64748B";
const CARD     = "FFFFFF";
const CODE_BG  = "0B1220";   // 代码块底
const CODE_TX  = "7DD3FC";   // 代码字
const CODE_AM  = "FBBF24";   // 代码强调
const CODE_GN  = "86EFAC";   // 代码绿
const RED      = "DC2626";

const F  = "Microsoft YaHei";   // 中文正文
const FB = "Microsoft YaHei";   // 标题（加粗用 bold）
const FC = "Consolas";          // 代码/协议字节

const W = 13.33, H = 7.5, M = 0.55;

/* ---------- 通用元素 ---------- */
function header(s, kicker, title) {
  s.addText(kicker, { x: M, y: 0.32, w: 9, h: 0.32, fontSize: 12, fontFace: F,
    color: PRIMARY, bold: true, charSpacing: 2, margin: 0 });
  s.addText(title, { x: M, y: 0.62, w: W - 2 * M, h: 0.72, fontSize: 30, fontFace: FB,
    bold: true, color: TEXT, margin: 0 });
  s.addText("手柄震动驱动 · 技术分享", { x: W - 2.4, y: 0.34, w: 1.9, h: 0.3, fontSize: 10,
    fontFace: F, color: MUTED, align: "right", margin: 0 });
}
function pageNo(s, n) {
  s.addText(String(n).padStart(2, "0"), { x: W - 1.0, y: H - 0.55, w: 0.5, h: 0.3,
    fontSize: 11, fontFace: FC, color: MUTED, align: "right", margin: 0 });
}
const shadow = () => ({ type: "outer", color: "0F172A", blur: 7, offset: 2, angle: 60, opacity: 0.13 });

/* 箭头（水平） */
function arrowR(s, x, y, len, color) {
  s.addShape(p.shapes.LINE, { x, y, w: len, h: 0, line: { color: color || PRIMARY, width: 2.2, endArrowType: "triangle" } });
}
function box(s, x, y, w, h, fill, line) {
  s.addShape(p.shapes.ROUNDED_RECTANGLE, { x, y, w, h, rectRadius: 0.055,
    fill: { color: fill }, line: line ? { color: line, width: 1 } : { type: "none" } });
}

/* ============ S1 封面 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG_DARK };
  // 装饰：协议字节雨（低调，右上）
  s.addText("03 0F 00 00 3C 3C 00 00 00 00 00 00 00 00 00", { x: 6.9, y: 0.7, w: 6.2, h: 0.4,
    fontSize: 14, fontFace: FC, color: PRIM_D, align: "right", margin: 0 });
  s.addText("05 5A 00 5A 00      hidraw write → motor", { x: 6.9, y: 1.1, w: 6.2, h: 0.4,
    fontSize: 14, fontFace: FC, color: "134E4A", align: "right", margin: 0 });
  s.addText("技 术 分 享 · A N D R O I D 底 层", { x: M, y: 1.7, w: 8, h: 0.4, fontSize: 13,
    fontFace: F, color: PRIMARY, bold: true, charSpacing: 3, margin: 0 });
  s.addText("让不支持的手柄震起来", { x: M, y: 2.15, w: 11.5, h: 1.15, fontSize: 55, fontFace: FB,
    bold: true, color: "FFFFFF", margin: 0 });
  s.addText("魅族21 × GameSir 启明星 / Stadia · 用户态震动驱动全案", { x: M, y: 3.45, w: 11, h: 0.55,
    fontSize: 21, fontFace: F, color: "94A3B8", margin: 0 });
  // 数据带
  const stats = [
    ["0", "内核模块编译量"],
    ["2", "协议逆向完成"],
    ["1:1", "游戏时序精度"],
    ["5.10–6.1", "内核验证跨度"],
  ];
  stats.forEach((st, i) => {
    const x = M + i * 3.1;
    s.addText(st[0], { x, y: 4.85, w: 2.9, h: 0.75, fontSize: 40, fontFace: FB, bold: true,
      color: i === 2 ? ACCENT : PRIMARY, margin: 0 });
    s.addText(st[1], { x, y: 5.62, w: 2.9, h: 0.35, fontSize: 12, fontFace: F, color: "94A3B8", margin: 0 });
  });
  s.addText("rumble_driver · uinput 代理 + hidraw 直写 · Magisk 模块", { x: M, y: 6.6, w: 10, h: 0.4,
    fontSize: 13, fontFace: FC, color: "475569", margin: 0 });
})();

/* ============ S2 问题：官方通路为什么是死路 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG };
  header(s, "01 · 问题定位", "官方通路为什么是死路");
  // 链条图（5环节，3断点）
  const chain = [
    ["游戏/模拟器", "调用震动API", true],
    ["系统框架", "InputDevice.getVibrator", true],
    ["力反馈(FF)", "uinput / evdev", false],
    ["HID 驱动", "hid-generic 不映射震动", false],
    ["手柄马达", "收不到输出报告", false],
  ];
  const bw = 2.18, gap = 0.35, y0 = 1.75;
  chain.forEach((c, i) => {
    const x = M + i * (bw + gap);
    box(s, x, y0, bw, 1.25, c[2] ? CARD : "FEF2F2");
    s.addText(c[0], { x: x + 0.12, y: y0 + 0.16, w: bw - 0.24, h: 0.4, fontSize: 15, fontFace: FB,
      bold: true, color: c[2] ? TEXT : RED, margin: 0 });
    s.addText(c[1], { x: x + 0.12, y: y0 + 0.6, w: bw - 0.24, h: 0.55, fontSize: 10.5, fontFace: F,
      color: c[2] ? MUTED : RED, margin: 0 });
    if (i < 4) arrowR(s, x + bw + 0.04, y0 + 0.62, gap - 0.08, c[2] ? PRIMARY : RED);
  });
  s.addText("断点 ×3", { x: M + 2 * (bw + gap) + 0.1, y: y0 + 1.4, w: 5, h: 0.35, fontSize: 12,
    fontFace: FB, bold: true, color: RED, margin: 0 });
  // 三条死因
  const rows = [
    ["hid-generic", "通用 HID 驱动不把手柄的厂商震动报告映射为系统 FF 能力 —— App 层 vibrators=0"],
    ["hid-microsoft", "GameSir X 模式伪装 Xbox（045E:0B13），但不在该驱动绑定名单，落回 hid-generic"],
    ["hid-google-stadiaff", "Stadia 震动驱动要 Linux 6.6+，手机内核 6.1 —— 直接缺席"],
  ];
  rows.forEach((r, i) => {
    const y = 3.65 + i * 1.02;
    s.addShape(p.shapes.OVAL, { x: M, y: y + 0.09, w: 0.16, h: 0.16, fill: { color: RED } });
    s.addText(r[0], { x: M + 0.34, y, w: 2.9, h: 0.4, fontSize: 15, fontFace: FC, bold: true, color: TEXT, margin: 0 });
    s.addText(r[1], { x: 3.9, y, w: 8.8, h: 0.8, fontSize: 13.5, fontFace: F, color: TEXT, margin: 0 });
    if (i < 2) s.addShape(p.shapes.LINE, { x: M, y: y + 0.88, w: W - 2 * M, h: 0, line: { color: "E2E8F0", width: 0.75 } });
  });
  s.addText("结论：蓝牙协议栈本身正常，卡在驱动层 —— 官方路径无法修复，必须自制驱动", {
    x: M, y: 6.72, w: 11.5, h: 0.4, fontSize: 14, fontFace: FB, bold: true, color: PRIM_D, margin: 0 });
  pageNo(s, 2);
})();

/* ============ S3 诊断三板斧 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG };
  header(s, "02 · 诊断方法", "三板斧定位断点");
  const cards = [
    ["App 层", "测试 App 直读能力", "InputDevice.getVibrator()\n→ vibrators = 0\n系统认为手柄不可震", "GamepadRumbleTest 自研 App\n设备卡 / 震动判定 / 事件监听"],
    ["内核层", "evdev 能力清单", "getevent -pl\n→ 手柄节点无 FF 段\n（手机自身马达节点有 FF 对照）", "确认：能力在内核就没暴露\n而不是上层没调用"],
    ["驱动层", "HID 驱动绑定", "/sys/bus/hid/drivers/\n→ microsoft/nintendo 在\n→ 但都没绑我们的手柄", "蓝牙双身份确认：\nBR/EDR 本体 + BLE Xbox 伪装"],
  ];
  cards.forEach((c, i) => {
    const x = M + i * 4.18, y = 1.75, w = 3.9, h = 4.3;
    box(s, x, y, w, h, CARD);
    s.addShape(p.shapes.RECTANGLE, { x: x + 0.28, y: y + 0.3, w: 0.34, h: 0.34, fill: { color: PRIMARY } });
    s.addText(String(i + 1), { x: x + 0.28, y: y + 0.28, w: 0.34, h: 0.36, fontSize: 15, fontFace: FB,
      bold: true, color: "FFFFFF", align: "center", margin: 0 });
    s.addText(c[0], { x: x + 0.78, y: y + 0.27, w: 2.6, h: 0.42, fontSize: 18, fontFace: FB, bold: true, color: TEXT, margin: 0 });
    s.addText(c[1], { x: x + 0.78, y: y + 0.66, w: 2.9, h: 0.32, fontSize: 11.5, fontFace: F, color: MUTED, margin: 0 });
    s.addShape(p.shapes.ROUNDED_RECTANGLE, { x: x + 0.28, y: y + 1.15, w: w - 0.56, h: 1.5,
      rectRadius: 0.04, fill: { color: CODE_BG } });
    s.addText(c[2], { x: x + 0.45, y: y + 1.3, w: w - 0.9, h: 1.25, fontSize: 12, fontFace: FC,
      color: CODE_TX, lineSpacing: 20, margin: 0 });
    s.addText(c[3], { x: x + 0.28, y: y + 2.85, w: w - 0.56, h: 1.3, fontSize: 12.5, fontFace: F,
      color: TEXT, lineSpacing: 19, margin: 0 });
  });
  s.addText("诊断产出决定路线 → 不编内核模块（需匹配 Flyme 源码），走 root 用户态 hidraw 直写", {
    x: M, y: 6.45, w: 12, h: 0.42, fontSize: 14, fontFace: FB, bold: true, color: PRIM_D, margin: 0 });
  pageNo(s, 3);
})();

/* ============ S4 核心架构 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG };
  header(s, "03 · 核心方案", "用户态驱动：uinput 代理 + hidraw 直写");
  const yA = 2.0, bh = 1.05;
  // 左：实体手柄
  box(s, M, yA + 0.55, 1.95, bh, "FFFFFF", "CBD5E1");
  s.addText("实体手柄", { x: M + 0.15, y: yA + 0.68, w: 1.7, h: 0.4, fontSize: 15, fontFace: FB, bold: true, color: TEXT, margin: 0 });
  s.addText("Xbox / Stadia 蓝牙", { x: M + 0.15, y: yA + 1.06, w: 1.75, h: 0.3, fontSize: 10, fontFace: F, color: MUTED, margin: 0 });
  // 中：驱动
  const dx = 3.35, dw = 5.1;
  box(s, dx, yA, dw, 2.2, PRIM_D);
  s.addText("rumble_driver 守护进程（root）", { x: dx + 0.25, y: yA + 0.14, w: dw - 0.5, h: 0.4,
    fontSize: 16, fontFace: FB, bold: true, color: "FFFFFF", margin: 0 });
  const feats = ["EVIOCGRAB 独占输入", "FF 翻译 · 限速 10-40Hz", "意图时间轴 · 到点即停", "归零兜底 · 断线自愈"];
  feats.forEach((f, i) => {
    const fx = dx + 0.25 + (i % 2) * 2.45, fy = yA + 0.68 + Math.floor(i / 2) * 0.62;
    s.addShape(p.shapes.OVAL, { x: fx, y: fy + 0.13, w: 0.1, h: 0.1, fill: { color: ACCENT } });
    s.addText(f, { x: fx + 0.2, y: fy, w: 2.3, h: 0.4, fontSize: 12, fontFace: F, color: "CCFBF1", margin: 0 });
  });
  // 右：虚拟手柄+游戏
  box(s, 9.3, yA - 0.05, 3.45, 1.0, CARD);
  s.addText("uinput 虚拟手柄（带 FF）", { x: 9.45, y: yA + 0.06, w: 3.2, h: 0.4, fontSize: 14, fontFace: FB, bold: true, color: PRIM_D, margin: 0 });
  s.addText("系统眼中：原生支持震动的手柄", { x: 9.45, y: yA + 0.46, w: 3.2, h: 0.34, fontSize: 10.5, fontFace: F, color: MUTED, margin: 0 });
  box(s, 9.3, yA + 1.2, 3.45, 1.0, CARD);
  s.addText("游戏 / 模拟器（Eden 等）", { x: 9.45, y: yA + 1.31, w: 3.2, h: 0.4, fontSize: 14, fontFace: FB, bold: true, color: TEXT, margin: 0 });
  s.addText("标准震动 API 直接可用", { x: 9.45, y: yA + 1.71, w: 3.2, h: 0.34, fontSize: 10.5, fontFace: F, color: MUTED, margin: 0 });
  // 输入箭头（左→右）
  arrowR(s, M + 2.0, yA + 1.07, 1.28, PRIMARY);
  s.addText("输入事件", { x: M + 1.95, y: yA + 0.62, w: 1.4, h: 0.32, fontSize: 10.5, fontFace: F, color: PRIMARY, align: "center", margin: 0 });
  arrowR(s, dx + dw + 0.05, yA + 0.45, 0.85, PRIMARY);
  s.addText("转发", { x: dx + dw + 0.05, y: yA + 0.08, w: 0.85, h: 0.3, fontSize: 10.5, fontFace: F, color: PRIMARY, align: "center", margin: 0 });
  // 震动回路（下方返回）
  s.addShape(p.shapes.LINE, { x: 10.95, y: yA + 2.25, w: 0, h: 0.75, line: { color: ACCENT, width: 2.2 } });
  s.addShape(p.shapes.LINE, { x: M + 0.98, y: yA + 3.0, w: 9.97, h: 0, line: { color: ACCENT, width: 2.2 } });
  s.addShape(p.shapes.LINE, { x: M + 0.98, y: yA + 2.2, w: 0, h: 0.8, line: { color: ACCENT, width: 2.2, beginArrowType: "triangle" } });
  s.addText("震动请求（FF 效果）→ 翻译成厂商报告 → hidraw 直写 → 马达", {
    x: 3.2, y: yA + 3.08, w: 7.2, h: 0.4, fontSize: 12.5, fontFace: FB, bold: true, color: ACCENT, align: "center", margin: 0 });
  // 底部三特性
  const bots = [
    ["隐藏原手柄", "unlink /dev/input/eventN\n系统只见虚拟手柄 = 第一个手柄"],
    ["暂停开关", "App 按钮 / Magisk 按钮\nmknod 字节级还原直连"],
    ["守护自愈", "模块开机自启\n断线 5 秒重连 · 崩溃兜底恢复"],
  ];
  bots.forEach((b, i) => {
    const x = M + i * 4.18;
    box(s, x, 5.85, 3.9, 1.25, "F1F5F9");
    s.addText(b[0], { x: x + 0.22, y: 5.98, w: 3.5, h: 0.38, fontSize: 14, fontFace: FB, bold: true, color: PRIM_D, margin: 0 });
    s.addText(b[1], { x: x + 0.22, y: 6.36, w: 3.55, h: 0.66, fontSize: 10.5, fontFace: F, color: TEXT, lineSpacing: 15, margin: 0 });
  });
  pageNo(s, 4);
})();

/* ============ S5 震动协议 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG };
  header(s, "04 · 协议逆向", "两只手柄的震动报告");
  // GameSir
  box(s, M, 1.7, 6.0, 2.6, CODE_BG);
  s.addText("GameSir 启明星 · X 模式 (045E:0B13)", { x: M + 0.25, y: 1.88, w: 5.5, h: 0.36,
    fontSize: 13.5, fontFace: FB, bold: true, color: "FFFFFF", margin: 0 });
  s.addText([
    { text: "03", options: { color: CODE_GN } },
    { text: " 0F ", options: { color: CODE_TX } },
    { text: "00 00 ", options: { color: "475569" } },
    { text: "3C 28", options: { color: CODE_AM } },
    { text: " 00 00 …", options: { color: "475569" } },
  ], { x: M + 0.25, y: 2.32, w: 5.5, h: 0.5, fontSize: 20, fontFace: FC, margin: 0 });
  s.addText("报告ID   电机使能            强  弱(0-100)", { x: M + 0.25, y: 2.86, w: 5.6, h: 0.3,
    fontSize: 10, fontFace: FC, color: "64748B", margin: 0 });
  s.addText("来源：xpadneo 社区逆向 + 4 种布局脉冲探测实测确认（原装 Xbox 格式）", {
    x: M + 0.25, y: 3.4, w: 5.5, h: 0.7, fontSize: 11.5, fontFace: F, color: "94A3B8", lineSpacing: 16, margin: 0 });
  // Stadia
  box(s, 6.95, 1.7, 5.85, 2.6, CODE_BG);
  s.addText("Google Stadia · 蓝牙 (18D1:9400)", { x: 7.2, y: 1.88, w: 5.3, h: 0.36,
    fontSize: 13.5, fontFace: FB, bold: true, color: "FFFFFF", margin: 0 });
  s.addText([
    { text: "05", options: { color: CODE_GN } },
    { text: " ", options: { color: CODE_TX } },
    { text: "5A 00", options: { color: CODE_AM } },
    { text: " ", options: { color: CODE_TX } },
    { text: "5A 00", options: { color: CODE_AM } },
  ], { x: 7.2, y: 2.32, w: 5.3, h: 0.5, fontSize: 20, fontFace: FC, margin: 0 });
  s.addText("报告ID   强(u16小端)   弱(u16小端)", { x: 7.2, y: 2.86, w: 5.3, h: 0.3,
    fontSize: 10, fontFace: FC, color: "64748B", margin: 0 });
  s.addText("来源：Linux 内核 hid-google-stadiaff.c 源码 + 字节级实测（05 80 80 80 80 = 50%起震）", {
    x: 7.2, y: 3.4, w: 5.4, h: 0.7, fontSize: 11.5, fontFace: F, color: "94A3B8", lineSpacing: 16, margin: 0 });
  // 归零语义
  box(s, M, 4.6, 12.25, 0.95, PRIM_T);
  s.addText([
    { text: "停止 = 发送全零报告", options: { bold: true, color: PRIM_D } },
    { text: "   ·   停止包永不放弃：EAGAIN 写失败 → 挂起补发，每 25ms 重试直到马达停转", options: { color: TEXT } },
  ], { x: M + 0.3, y: 4.6, w: 11.7, h: 0.95, fontSize: 14.5, fontFace: F, valign: "middle", margin: 0 });
  // 踩坑提示
  s.addText("协议层踩坑：hidraw_devinfo 的 VID/PID 是 __s16 有符号 —— Stadia PID 0x9400 读出为负数，比较前必须转 uint16_t", {
    x: M, y: 5.85, w: 12.2, h: 0.75, fontSize: 12.5, fontFace: F, color: MUTED, lineSpacing: 18, margin: 0 });
  pageNo(s, 5);
})();

/* ============ S6 时序攻坚 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG };
  header(s, "05 · 时序攻坚", "四场战役：让震动「该停就停」");
  const rows = [
    ["震动不停", "BLE 忙时 EAGAIN，停止包被丢弃 → 马达卡在最后强度", "need_zero 挂起补发，永不放弃"],
    ["慢一拍", "写失败重试阻塞事件循环最长 180ms", "内联重试 10ms + 25ms 轮询补发"],
    ["高密流打爆", "游戏 200Hz 振幅流直写 hidraw，BLE 队列积压", "自适应限速 10-40Hz + 流式合并 + 噪声门"],
    ["10s 震成 29s", "AOSP 震动会话排队，晚到事件不断顺延结束时刻", "意图时间轴：结束 = 锚点 + 累计时长，到点即停"],
  ];
  const yT = 1.78, rh = 1.12;
  // 表头
  s.addText("症状", { x: M, y: yT, w: 2.3, h: 0.4, fontSize: 13, fontFace: FB, bold: true, color: MUTED, margin: 0 });
  s.addText("根因", { x: 3.1, y: yT, w: 5.3, h: 0.4, fontSize: 13, fontFace: FB, bold: true, color: MUTED, margin: 0 });
  s.addText("修复", { x: 8.7, y: yT, w: 4, h: 0.4, fontSize: 13, fontFace: FB, bold: true, color: MUTED, margin: 0 });
  s.addShape(p.shapes.LINE, { x: M, y: yT + 0.42, w: W - 2 * M, h: 0, line: { color: "CBD5E1", width: 1 } });
  rows.forEach((r, i) => {
    const y = yT + 0.58 + i * rh;
    s.addText(r[0], { x: M, y: y + 0.1, w: 2.3, h: 0.7, fontSize: 16, fontFace: FB, bold: true, color: RED, margin: 0 });
    s.addText(r[1], { x: 3.1, y: y + 0.06, w: 5.35, h: 0.95, fontSize: 13, fontFace: F, color: TEXT, lineSpacing: 19, margin: 0 });
    box(s, 8.7, y + 0.02, 4.05, 0.92, i === 3 ? "FEF3C7" : PRIM_T);
    s.addText(r[2], { x: 8.88, y: y + 0.02, w: 3.75, h: 0.92, fontSize: 12.5, fontFace: FB,
      bold: true, color: i === 3 ? ACCENT : PRIM_D, valign: "middle", lineSpacing: 17, margin: 0 });
    if (i < 3) s.addShape(p.shapes.LINE, { x: M, y: y + rh - 0.06, w: W - 2 * M, h: 0, line: { color: "E2E8F0", width: 0.75 } });
  });
  s.addText("方法论：毫秒级时间戳日志 + awk 震动段/间隙分析 —— 间隙 <25ms = 游戏连续请求；大间隙 + 晚停 = 链路问题", {
    x: M, y: 6.78, w: 12.2, h: 0.4, fontSize: 12, fontFace: F, color: MUTED, margin: 0 });
  pageNo(s, 6);
})();

/* ============ S7 框架特性实测 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG };
  header(s, "06 · 平台特性", "AOSP 震动会话队列实测");
  // 左：三个实测行为
  const beh = [
    ["Eden 实时流", "按段节奏调用 → 1:1 精确转发", CODE_GN],
    ["30ms 会话流", "排队拉伸 1.5–2.9×（10s→13~29s）", CODE_AM],
    ["Waveform 批发", "300 步 7ms 内倾泻完毕 = 无感", RED],
  ];
  beh.forEach((b, i) => {
    const y = 1.8 + i * 1.32;
    box(s, M, y, 6.55, 1.14, CARD);
    s.addShape(p.shapes.OVAL, { x: M + 0.25, y: y + 0.42, w: 0.26, h: 0.26, fill: { color: b[2] } });
    s.addText(b[0], { x: M + 0.68, y: y + 0.13, w: 5.6, h: 0.42, fontSize: 15.5, fontFace: FB, bold: true, color: TEXT, margin: 0 });
    s.addText(b[1], { x: M + 0.68, y: y + 0.58, w: 5.7, h: 0.42, fontSize: 12.5, fontFace: F, color: MUTED, margin: 0 });
  });
  // 右：结论卡
  box(s, 7.6, 1.8, 5.2, 3.9, BG_DARK);
  s.addText("对策", { x: 7.9, y: 2.05, w: 4.6, h: 0.4, fontSize: 15, fontFace: FB, bold: true, color: PRIMARY, margin: 0 });
  const ops = [
    "驱动侧：意图时间轴兜底 —— 上游怎么拖，结束时刻不变",
    "App 侧：高频模式改实时节奏化小段调用",
    "可选：LSPosed 钩子 + 抽象域 socket 直通（已实现可开关）",
  ];
  ops.forEach((o, i) => {
    const y = 2.55 + i * 0.98;
    s.addShape(p.shapes.OVAL, { x: 7.9, y: y + 0.1, w: 0.12, h: 0.12, fill: { color: ACCENT } });
    s.addText(o, { x: 8.12, y, w: 4.5, h: 0.95, fontSize: 12.5, fontFace: F, color: "E2E8F0", lineSpacing: 18, margin: 0 });
  });
  s.addText("游戏实测 Eden 时间戳 1:1 —— 真实游戏不受会话队列影响", {
    x: M, y: 6.15, w: 12, h: 0.45, fontSize: 14, fontFace: FB, bold: true, color: PRIM_D, margin: 0 });
  s.addText("Stadia BLE 物理延迟（几十毫秒级）不可软件消除，插线 USB 即满血 —— 与 PC 对齐需私有接口（StadiaLink 思路）", {
    x: M, y: 6.68, w: 12.2, h: 0.4, fontSize: 12, fontFace: F, color: MUTED, margin: 0 });
  pageNo(s, 7);
})();

/* ============ S8 验证与移植 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG };
  header(s, "07 · 验证与移植", "实测结果与适用范围");
  // 左列大数字
  const nums = [
    ["1 : 1", "Eden 游戏内震动时序\n（时间戳逐段对账）"],
    ["2 台", "真机验证\n6.1/安卓16 · 5.10/安卓17"],
    ["3 包", "Magisk 模块交付\n合集 / 仅小鸡 / 仅Stadia"],
  ];
  nums.forEach((n, i) => {
    const y = 1.75 + i * 1.62;
    s.addText(n[0], { x: M, y, w: 2.6, h: 0.85, fontSize: 44, fontFace: FB, bold: true, color: PRIMARY, margin: 0 });
    s.addText(n[1], { x: 3.35, y: y + 0.16, w: 3.0, h: 0.8, fontSize: 12.5, fontFace: F, color: TEXT, lineSpacing: 18, margin: 0 });
  });
  // 右：移植要求
  box(s, 6.9, 1.75, 5.9, 4.6, CARD);
  s.addText("移植到其他手机", { x: 7.15, y: 1.95, w: 5.3, h: 0.42, fontSize: 16, fontFace: FB, bold: true, color: TEXT, margin: 0 });
  const req = [
    ["必备", "Root（Magisk/KernelSU/SukiSU）", PRIM_D],
    ["必备", "Android 12+（手柄震动 API 起点）", PRIM_D],
    ["必备", "/dev/hidraw 可写（绝大多数 ROM 满足）", PRIM_D],
    ["风险", "个别 ROM 蓝牙 VID/PID 清零 → 加名字兜底", ACCENT],
    ["退化", "SELinux 拦 mknod → 隐藏功能降级，震动不受影响", MUTED],
  ];
  req.forEach((r, i) => {
    const y = 2.5 + i * 0.72;
    box(s, 7.15, y + 0.05, 0.72, 0.4, r[2] === ACCENT ? "FEF3C7" : (r[2] === MUTED ? "F1F5F9" : PRIM_T));
    s.addText(r[0], { x: 7.15, y: y + 0.05, w: 0.72, h: 0.4, fontSize: 11, fontFace: FB, bold: true,
      color: r[2], align: "center", valign: "middle", margin: 0 });
    s.addText(r[1], { x: 8.02, y: y + 0.03, w: 4.6, h: 0.5, fontSize: 12.5, fontFace: F, color: TEXT, margin: 0 });
  });
  s.addShape(p.shapes.ROUNDED_RECTANGLE, { x: 7.15, y: 5.72, w: 5.4, h: 0.5, rectRadius: 0.04, fill: { color: CODE_BG } });
  s.addText("$ adb shell ls /dev/hidraw   # 买前 10 秒自检", { x: 7.3, y: 5.72, w: 5.2, h: 0.5,
    fontSize: 11.5, fontFace: FC, color: CODE_GN, valign: "middle", margin: 0 });
  pageNo(s, 8);
})();

/* ============ S9 总结 ============ */
(() => {
  const s = p.addSlide();
  s.background = { color: BG_DARK };
  s.addText("总 结", { x: M, y: 0.55, w: 4, h: 0.45, fontSize: 13, fontFace: FB, bold: true,
    color: PRIMARY, charSpacing: 3, margin: 0 });
  s.addText("「系统不支持」的四层拆解，全在用户态闭环", { x: M, y: 0.98, w: 12, h: 0.85,
    fontSize: 30, fontFace: FB, bold: true, color: "FFFFFF", margin: 0 });
  const takes = [
    ["诊断先于编码", "App/内核/驱动三层取证，把「不支持」拆成可修的断点，再选路线"],
    ["用户态能到内核到不了的地方", "hidraw 直写 + uinput 代理，零内核编译，跨版本通吃"],
    ["时序正确性要自己兜底", "意图时间轴 + 归零补发 + 自适应限速 —— 上游再乱，到点就停"],
    ["交付物即诊断工具", "测试 App（可视化/时序/震动图库）与驱动同生长，问题必可复现"],
  ];
  takes.forEach((t, i) => {
    const x = M + (i % 2) * 6.3, y = 2.15 + Math.floor(i / 2) * 1.75;
    s.addShape(p.shapes.RECTANGLE, { x, y: y + 0.06, w: 0.3, h: 0.3, fill: { color: PRIMARY } });
    s.addText(String(i + 1), { x, y: y + 0.04, w: 0.3, h: 0.32, fontSize: 13, fontFace: FB, bold: true, color: "FFFFFF", align: "center", margin: 0 });
    s.addText(t[0], { x: x + 0.45, y, w: 5.6, h: 0.45, fontSize: 17, fontFace: FB, bold: true, color: "FFFFFF", margin: 0 });
    s.addText(t[1], { x: x + 0.45, y: y + 0.5, w: 5.6, h: 0.95, fontSize: 12.5, fontFace: F, color: "94A3B8", lineSpacing: 18, margin: 0 });
  });
  s.addShape(p.shapes.LINE, { x: M, y: 5.85, w: W - 2 * M, h: 0, line: { color: "1E293B", width: 1 } });
  s.addText([
    { text: "交付：", options: { bold: true, color: PRIMARY } },
    { text: "rumble_driver v2.3（gamesir_rumble.c）· 3 个 Magisk 模块 zip · GamepadRumbleTest 测试 App · 全套踩坑档案", options: { color: "CBD5E1" } },
  ], { x: M, y: 6.05, w: 12.3, h: 0.45, fontSize: 13.5, fontFace: F, margin: 0 });
  s.addText("谢谢 · Q&A", { x: M, y: 6.65, w: 6, h: 0.5, fontSize: 15, fontFace: FB, bold: true, color: "475569", margin: 0 });
})();

p.writeFile({ fileName: require("path").join(__dirname, "..", "手柄震动驱动-技术分享.pptx") })
  .then(() => console.log("PPT written"));
