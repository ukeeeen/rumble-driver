/*
 * gamesir_rumble.c — GameSir Nova Lite / Google Stadia 手柄震动驱动守护进程 v1.1
 *
 * 原理：手柄蓝牙/有线连接时内核 hid-generic 不映射 FF，安卓应用无法触发震动。
 * 本进程以 root 运行，直接经 /dev/hidraw 发送厂商震动报告驱动马达，
 * 并通过 uinput 建立带 FF 的虚拟手柄代理全部输入，游戏对虚拟手柄调震动即可。
 *
 * 已验证协议：
 *  GameSir Nova Lite X模式(045E:0B13) — report 0x03:
 *    [0]=0x03 [1]=0x0F [2..3]=0 [4]=强0-100 [5]=弱0-100 （15字节,实测布局）
 *  Stadia(18D1:9400) — report 0x05:
 *    [0]=0x05 [1..2]=强u16小端 [3..4]=弱u16小端 （内核 hid-google-stadiaff.c）
 *
 * 用法：
 *   gamesir_rumble --info
 *   gamesir_rumble --oneshot <强0-100> <弱0-100> <持续ms>
 *   gamesir_rumble --probe
 *   gamesir_rumble --tap [强度] [持续ms]
 *   gamesir_rumble --uinput [ --layout N ]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <linux/uinput.h>

#define LOGI(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOGE(...) do { fprintf(stderr, "E: " __VA_ARGS__); fputc('\n', stderr); } while (0)

#define MAX_PADS 4
#define MAX_EFFECTS 32
#define REPORT_LEN 15

enum pad_type { PAD_NONE = 0, PAD_GAMESIR_X, PAD_GAMESIR_ANDROID, PAD_STADIA };

struct pad_type_info {
    int type;
    const char *label;
    const char *uname;      /* uinput 虚拟设备名 */
    int bustype;            /* uinput id.bustype */
    int vendor, product;    /* uinput id vendor/product */
};
static const struct pad_type_info TYPE_INFO[] = {
    { PAD_GAMESIR_X,       "GameSir Nova Lite X模式",  "GameSir Nova Lite Rumble", 0x0003, 0x045e, 0x0b13 },
    { PAD_GAMESIR_ANDROID, "GameSir Nova Lite 安卓",   "GameSir Nova Lite Rumble", 0x0003, 0x045e, 0x0b13 },
    { PAD_STADIA,          "Google Stadia Controller", "Stadia Controller Rumble", 0x0003, 0x18d1, 0x9400 },
};

struct pad {
    int type;
    int hid_fd, ev_fd, ui_fd;
    int hid_idx, ev_idx;
    int ev_major, ev_minor, hidden;
    char ev_path[32];
    char ev_name[80];
    struct ff_effect effects[MAX_EFFECTS];
    int64_t stop_at;
    int last_s, last_w;                /* 上次已发送的强度（流式去重） */
    int64_t last_send;                 /* 上次实际写hidraw时刻（限速） */
    int want_s, want_w, dirty;         /* 待发振幅（合并200Hz流） */
    int need_zero;                     /* 归零包发送失败挂起，主循环补发直到成功 */
    int min_send_ms;                   /* 自适应发送间隔（10~40ms） */
    int ok_streak;                     /* 连续无压力发送计数 */
    int64_t stream_anchor;             /* 震动流锚点：意图时间轴起点 */
    int64_t stream_pos;                /* 流内累计请求时长 */
    int64_t stream_last_ev;            /* 最近一次播放事件（>500ms空档重置流） */
    int64_t stream_dead;               /* 流到期后的静默期：迟到散包直接丢弃 */
};

/* 输出限速：游戏(Eden/Switch HD震动)以200Hz流式更新振幅，BLE通道扛不住。
 * 自适应：默认25ms，连续40次发送无EAGAIN则收紧(-3ms，下限10ms)；
 * 撞EAGAIN立即退让(+8ms，上限40ms)。停止包不受限速。 */
#define MIN_SEND_MS 25
static void reset_pad_link(struct pad *p) { p->min_send_ms = MIN_SEND_MS; p->ok_streak = 0; }

static int64_t now_ms(void);
static int rumble_send(struct pad *p, int strong, int weak);
static const char *type_uname(int t);
static void stream_extend(struct pad *p, int dur);
static void pad_flush(struct pad *p) {
    if (!p->dirty) return;
    int64_t now = now_ms();
    if (now - p->last_send < p->min_send_ms) return;
    /* 噪声门：变化≥5% 或基准无效(刚停止过)才发包；不足门槛保持dirty继续累积 */
    if (p->last_s < 0 || abs(p->want_s - p->last_s) >= 5 || abs(p->want_w - p->last_w) >= 5) {
        if (rumble_send(p, p->want_s, p->want_w) == 0) {
            p->last_s = p->want_s; p->last_w = p->want_w;
            p->need_zero = 0;
            p->dirty = 0;
            p->last_send = now;
        }
        /* 发送失败：保持 dirty，下轮立即重试 */
    } else {
        p->last_send = now;   /* 门槛不足：仅刷新节拍，dirty保留等待振幅累积 */
    }
}
static struct pad pads[MAX_PADS];

static int report_layout = 4;          /* GameSir实测=4(Xbox格式); Stadia: 1=小端 2=大端 */
static int pad_filter = 0x7;           /* 允许的手柄类型位掩码，--only 可改 */

/* ---------------- 原设备节点隐藏 ----------------
 * rm /dev/input/eventN 让系统框架不再看到实体手柄（虚拟手柄成为唯一/第一个手柄），
 * 已打开的 fd 与 EVIOCGRAB 独占不受影响；恢复=mknod 回原路径。
 * 崩溃兜底：隐藏状态写入 state 文件，启动时若发现残留则先恢复。 */
#define HIDE_STATE_FILE "/data/local/tmp/rumble_hidden_nodes"

static void hide_state_save(void) {
    FILE *f = fopen(HIDE_STATE_FILE, "w");
    if (!f) return;
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i].hidden)
            fprintf(f, "%d %d %s\n", pads[i].ev_major, pads[i].ev_minor, pads[i].ev_path);
    fclose(f);
}
static void hide_node(struct pad *p) {
    struct stat st;
    if (fstat(p->ev_fd, &st) < 0) return;
    p->ev_major = major(st.st_rdev);
    p->ev_minor = minor(st.st_rdev);
    snprintf(p->ev_path, sizeof(p->ev_path), "/dev/input/event%d", p->ev_idx);
    if (unlink(p->ev_path) == 0) {
        p->hidden = 1;
        hide_state_save();
        LOGI("已隐藏原设备节点 %s (maj=%d min=%d)，系统仅见虚拟手柄", p->ev_path, p->ev_major, p->ev_minor);
    } else {
        LOGE("隐藏 %s 失败: %s", p->ev_path, strerror(errno));
    }
}
static void unhide_node(struct pad *p) {
    if (!p->hidden) return;
    /* 设备已拔除则不重建节点（devtmpfs 已清理，避免留下死节点） */
    int ver = 0;
    if (p->ev_fd >= 0 && ioctl(p->ev_fd, EVIOCGVERSION, &ver) == 0) {
        unlink(p->ev_path);
        if (mknod(p->ev_path, S_IFCHR | 0660, makedev(p->ev_major, p->ev_minor)) == 0) {
            chown(p->ev_path, 0, 1004);   /* root:input(gid 1004)，与原生节点一致 */
            chmod(p->ev_path, 0660);
            LOGI("已恢复原设备节点 %s", p->ev_path);
        } else {
            LOGE("恢复 %s 失败: %s", p->ev_path, strerror(errno));
        }
    }
    p->hidden = 0;
    hide_state_save();
}
static void unhide_stale_at_boot(void) {
    FILE *f = fopen(HIDE_STATE_FILE, "r");
    if (!f) return;
    int maj, min, n = 0;
    char path[64];
    while (fscanf(f, "%d %d %63s", &maj, &min, path) == 3) {
        unlink(path);
        if (mknod(path, S_IFCHR | 0660, makedev(maj, min)) == 0) {
            chown(path, 0, 1004);
            n++;
        }
    }
    fclose(f);
    unlink(HIDE_STATE_FILE);
    if (n) LOGI("启动恢复 %d 个被隐藏的节点（上次异常退出）", n);
}

static int64_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
/* 带时间戳的日志（实时钟） */
static void tstamp(char *out, size_t n) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm; localtime_r(&ts.tv_sec, &tm);
    snprintf(out, n, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(ts.tv_nsec / 1000000));
}
#define LOGT(...) do { char _ts[20]; tstamp(_ts, sizeof(_ts)); fprintf(stderr, "%s ", _ts); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)

/* ---------------- 震动报告 ---------------- */

static void stop_rumble_and_exit(int code) {
    uint8_t buf[REPORT_LEN];
    for (int i = 0; i < MAX_PADS; i++) {
        if (pads[i].type && pads[i].hid_fd >= 0) {
            int n = 0;
            if (pads[i].type == PAD_STADIA) {
                memset(buf, 0, 5); buf[0] = 0x05; n = 5;
            } else {
                memset(buf, 0, REPORT_LEN); buf[0] = 0x03; n = REPORT_LEN;
            }
            write(pads[i].hid_fd, buf, n);
        }
        if (pads[i].hidden) unhide_node(&pads[i]);
    }
    exit(code);
}
static void on_signal(int _) { (void)_; stop_rumble_and_exit(0); }

static int rumble_send(struct pad *p, int strong_pct, int weak_pct) {
    uint8_t buf[REPORT_LEN];
    int n;
    if (p->type == PAD_STADIA) {
        int s = strong_pct * 0xffff / 100, w = weak_pct * 0xffff / 100;
        if (report_layout == 2) { /* 大端变体 */
            buf[0] = 0x05;
            buf[1] = s >> 8; buf[2] = s & 0xff;
            buf[3] = w >> 8; buf[4] = w & 0xff;
        } else {
            buf[0] = 0x05;
            buf[1] = s & 0xff; buf[2] = s >> 8;
            buf[3] = w & 0xff; buf[4] = w >> 8;
        }
        n = 5;
    } else {
        memset(buf, 0, REPORT_LEN);
        buf[0] = 0x03;
        switch (report_layout) {
        case 2:  buf[1] = 0x01; buf[2] = strong_pct; buf[3] = weak_pct; break;
        case 3:  buf[10] = strong_pct; buf[11] = weak_pct; break;
        case 4:  buf[1] = 0x0f; buf[4] = strong_pct; buf[5] = weak_pct; break;
        default: buf[1] = 0x0f; buf[2] = strong_pct; buf[3] = weak_pct; break;
        }
        n = REPORT_LEN;
    }
    ssize_t r = -1;
    int eagain = 0;
    for (int tryn = 0; tryn < 4; tryn++) {            /* 内联重试≤10ms，剩余交主循环补发 */
        r = write(p->hid_fd, buf, n);
        if (r >= 0) break;
        if (errno == EAGAIN || errno == EBUSY) { eagain = 1; usleep(3000); continue; }
        break;
    }
    if (r < 0 && eagain) {
        LOGT("[%s] 写入EAGAIN放弃(强度=%d,%d)", type_uname(p->type), strong_pct, weak_pct);
        /* 自适应退让：链路有压力，放宽发送间隔 */
        if (p->min_send_ms < 40) {
            int old = p->min_send_ms;
            p->min_send_ms += 8; if (p->min_send_ms > 40) p->min_send_ms = 40;
            LOGT("[%s] 限速退让 %d→%dms", type_uname(p->type), old, p->min_send_ms);
        }
        p->ok_streak = 0;
    }
    if (r < 0) {
        if (errno != EAGAIN && errno != EBUSY) {
            LOGE("write hidraw 失败: %s", strerror(errno));
            if (errno == ENODEV || errno == EPIPE || errno == EIO || errno == EBADF)
                stop_rumble_and_exit(42);
        }
        return -1;
    }
    /* 软性压力：重试后才成功，同样视为链路吃紧，退让并重新积累 */
    if (r >= 0 && eagain) {
        p->ok_streak = 0;
        if (p->min_send_ms < 40) {
            p->min_send_ms += 4; if (p->min_send_ms > 40) p->min_send_ms = 40;
        }
    }
    /* 干净送达（无重试）：累计后尝试收紧间隔 */
    if (!eagain && ++p->ok_streak >= 40 && p->min_send_ms > 10) {
        int old = p->min_send_ms;
        p->min_send_ms -= 3; if (p->min_send_ms < 10) p->min_send_ms = 10;
        LOGT("[%s] 限速收紧 %d→%dms", type_uname(p->type), old, p->min_send_ms);
        p->ok_streak = 0;
    }
    return 0;
}

/* ---------------- 设备发现与配对 ---------------- */

static int classify(int bustype, int vendor, int product) {
    int t = PAD_NONE;
    if (vendor == 0x3537 && product == 0x1041) t = PAD_GAMESIR_ANDROID;
    else if (vendor == 0x045e && product == 0x0b13) t = PAD_GAMESIR_X;
    else if (vendor == 0x18d1 && product == 0x9400) t = PAD_STADIA;
    if (t && !(t & pad_filter)) return PAD_NONE;   /* 被 --only 过滤 */
    return t;
}
static const char *type_label(int t) {
    for (size_t i = 0; i < sizeof(TYPE_INFO) / sizeof(TYPE_INFO[0]); i++)
        if (TYPE_INFO[i].type == t) return TYPE_INFO[i].label;
    return "?";
}
static const char *type_uname(int t) {
    for (size_t i = 0; i < sizeof(TYPE_INFO) / sizeof(TYPE_INFO[0]); i++)
        if (TYPE_INFO[i].type == t) return TYPE_INFO[i].uname;
    return "Rumble Pad";
}

static int discover(void) {
    struct { int fd, node, bus, vendor, product; int type; } hids[MAX_PADS];
    int nh = 0;
    char path[32];

    memset(pads, 0, sizeof(pads));
    for (int i = 0; i < MAX_PADS; i++) {
        pads[i].hid_fd = pads[i].ev_fd = pads[i].ui_fd = -1;
        pads[i].hid_idx = pads[i].ev_idx = -1;
        reset_pad_link(&pads[i]);
    }

    /* 收集匹配的 hidraw */
    for (int i = 0; i < 16 && nh < MAX_PADS; i++) {
        snprintf(path, sizeof(path), "/dev/hidraw%d", i);
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        struct hidraw_devinfo info;
        ioctl(fd, HIDIOCGRAWINFO, &info);
        /* hidraw 的 vid/pid 是 __s16，0x8000 以上为负数，须转无符号 */
        int t = classify(info.bustype, (uint16_t)info.vendor, (uint16_t)info.product);
        if (t) {
            hids[nh].fd = fd; hids[nh].node = i; hids[nh].bus = info.bustype;
            hids[nh].vendor = info.vendor; hids[nh].product = info.product;
            hids[nh].type = t; nh++;
        } else close(fd);
    }
    if (nh == 0) return 0;

    /* evdev 按 (bus,vid,pid) 配对；同节点只绑一次 */
    for (int e = 0; e < 64; e++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", e);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        struct input_id id;
        if (ioctl(fd, EVIOCGID, &id) < 0) { close(fd); continue; }
        int t = classify(id.bustype, id.vendor, id.product);
        if (!t) { close(fd); continue; }
        /* 排除自身创建的虚拟手柄（名字含 Rumble），防止代理套代理 */
        char nm[80] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(nm) - 1), nm);
        if (strstr(nm, "Rumble")) { close(fd); continue; }
        int dup = 0;
        for (int s = 0; s < MAX_PADS; s++) if (pads[s].ev_idx == e) dup = 1;
        if (dup) { close(fd); continue; }
        int bound = 0;
        for (int k = 0; k < nh; k++) {
            int used = 0;
            for (int s = 0; s < MAX_PADS; s++)
                if (pads[s].hid_idx == hids[k].node) used = 1;
            if (used) continue;
            int slot = -1;
            for (int s = 0; s < MAX_PADS; s++)
                if (!pads[s].type) { slot = s; break; }
            if (slot < 0) break;
            ioctl(fd, EVIOCGNAME(sizeof(pads[slot].ev_name) - 1), pads[slot].ev_name);
            pads[slot].type = t;
            pads[slot].hid_fd = hids[k].fd;
            pads[slot].hid_idx = hids[k].node;
            pads[slot].ev_fd = fd;
            pads[slot].ev_idx = e;
            bound = 1;
            LOGI("配对: hidraw%d ↔ event%d [%s] bus=%04x vid=%04x pid=%04x ev=%s",
                 hids[k].node, e, type_label(t), hids[k].bus, hids[k].vendor, hids[k].product,
                 pads[slot].ev_name);
            break;
        }
        if (!bound) close(fd);   /* 多余的同类节点(Consumer Control等)不代理 */
    }
    int count = 0;
    for (int s = 0; s < MAX_PADS; s++) if (pads[s].type && pads[s].ev_fd >= 0) count++;
    /* 没配到 evdev 的 hidraw 关闭 */
    for (int k = 0; k < nh; k++) {
        int used = 0;
        for (int s = 0; s < MAX_PADS; s++)
            if (pads[s].hid_idx == hids[k].node) used = 1;
        if (!used) close(hids[k].fd);
    }
    return count;
}

/* ---------------- 简单模式 ---------------- */

static int mode_info(void) {
    int n = discover();
    LOGI("发现 %d 只手柄", n);
    return n > 0 ? 0 : 1;
}

static int mode_oneshot(int s, int w, int ms) {
    if (discover() == 0) { LOGE("未找到手柄"); return 1; }
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal);
    struct pad *p = &pads[0];
    LOGI("震动: [%s] 强=%d 弱=%d %dms", type_label(p->type), s, w, ms);
    rumble_send(p, s, w);
    usleep(ms * 1000);
    rumble_send(p, 0, 0);
    LOGI("完成");
    return 0;
}

static int mode_probe(void) {
    if (discover() == 0) { LOGE("未找到手柄"); return 1; }
    struct pad *p = &pads[0];
    int layouts = (p->type == PAD_STADIA) ? 2 : 4;
    int save = report_layout;
    for (int layout = 1; layout <= layouts; layout++) {
        report_layout = layout;
        for (int pulse = 0; pulse < layout; pulse++) {
            rumble_send(p, 90, 90);
            usleep(250 * 1000);
            rumble_send(p, 0, 0);
            usleep(250 * 1000);
        }
        LOGI("已发送布局%d（%d次脉冲）", layout, layout);
        sleep(2);
    }
    report_layout = save;
    LOGI("probe 完成：哪一轮脉冲次数与轮次号一致，即为正确布局");
    return 0;
}

static int mode_tap(int strength, int ms) {
    if (discover() == 0) { LOGE("未找到手柄"); return 1; }
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal);
    struct pad *p = &pads[0];
    LOGI("tap 模式启动: [%s] 按键即震(强=%d,%dms)", type_label(p->type), strength, ms);
    struct input_event ev;
    int64_t stop_at = 0;
    struct pollfd pf[1] = {{p->ev_fd, POLLIN, 0}};
    for (;;) {
        int timeout = stop_at > 0 ? (int)(stop_at - now_ms()) : -1;
        if (timeout < 0) timeout = stop_at > 0 ? 0 : -1;
        poll(pf, 1, timeout);
        while (read(p->ev_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1 &&
                ev.code >= BTN_JOYSTICK && ev.code <= BTN_TRIGGER_HAPPY40) {
                rumble_send(p, strength, strength);
                stop_at = now_ms() + ms;
            }
        }
        if (stop_at > 0 && now_ms() >= stop_at) {
            rumble_send(p, 0, 0);
            stop_at = 0;
        }
    }
}

/* ---------------- uinput 代理（完整驱动） ---------------- */

static int create_uinput(struct pad *p) {
    int u = open("/dev/uinput", O_RDWR | O_NONBLOCK);
    if (u < 0) { LOGE("打开 /dev/uinput 失败: %s", strerror(errno)); return -1; }
    ioctl(u, UI_SET_EVBIT, EV_SYN); ioctl(u, UI_SET_EVBIT, EV_KEY);
    ioctl(u, UI_SET_EVBIT, EV_ABS); ioctl(u, UI_SET_EVBIT, EV_FF);

    uint8_t keybits[KEY_MAX / 8 + 1] = {0}, absbits[ABS_MAX / 8 + 1] = {0};
    ioctl(p->ev_fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    ioctl(p->ev_fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
    for (int b = 0; b <= KEY_MAX; b++)
        if (keybits[b / 8] & (1 << (b % 8))) ioctl(u, UI_SET_KEYBIT, b);
    for (int b = 0; b <= ABS_MAX; b++) {
        if (absbits[b / 8] & (1 << (b % 8))) {
            ioctl(u, UI_SET_ABSBIT, b);
            struct uinput_abs_setup as;
            memset(&as, 0, sizeof(as));
            as.code = b;
            ioctl(p->ev_fd, EVIOCGABS(b), &as.absinfo);
            ioctl(u, UI_ABS_SETUP, &as);
        }
    }
    ioctl(u, UI_SET_FFBIT, FF_RUMBLE);
    ioctl(u, UI_SET_FFBIT, FF_PERIODIC);
    ioctl(u, UI_SET_FFBIT, FF_SQUARE);
    ioctl(u, UI_SET_FFBIT, FF_TRIANGLE);
    ioctl(u, UI_SET_FFBIT, FF_SINE);
    ioctl(u, UI_SET_FFBIT, FF_GAIN);

    struct uinput_setup us;
    memset(&us, 0, sizeof(us));
    strncpy(us.name, type_uname(p->type), UINPUT_MAX_NAME_SIZE - 1);
    us.id.bustype = 0x0003;
    for (size_t i = 0; i < sizeof(TYPE_INFO) / sizeof(TYPE_INFO[0]); i++)
        if (TYPE_INFO[i].type == p->type) {
            us.id.vendor = TYPE_INFO[i].vendor;
            us.id.product = TYPE_INFO[i].product;
        }
    us.id.version = 1;
    us.ff_effects_max = MAX_EFFECTS;
    if (ioctl(u, UI_DEV_SETUP, &us) < 0 || ioctl(u, UI_DEV_CREATE, 0) < 0) {
        LOGE("uinput 创建失败: %s", strerror(errno));
        close(u);
        return -1;
    }
    LOGI("uinput 代理已创建: [%s] ← %s", type_uname(p->type), p->ev_name);
    return u;
}

static void handle_uinput_read(struct pad *p) {
    struct input_event ev;
    while (read(p->ui_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
            struct uinput_ff_upload upload;
            memset(&upload, 0, sizeof(upload));
            upload.request_id = ev.value;
            ioctl(p->ui_fd, UI_BEGIN_FF_UPLOAD, &upload);
            int id = upload.effect.id;
            if (id >= 0 && id < MAX_EFFECTS) {
                p->effects[id] = upload.effect;
                LOGI("[%s] FF 上传 id=%d type=0x%x 强=%u 弱=%u 时长=%dms",
                     type_uname(p->type), id, upload.effect.type,
                     upload.effect.u.rumble.strong_magnitude,
                     upload.effect.u.rumble.weak_magnitude,
                     upload.effect.replay.length);
            }
            upload.retval = 0;
            ioctl(p->ui_fd, UI_END_FF_UPLOAD, &upload);
        } else if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
            struct uinput_ff_erase erase;
            memset(&erase, 0, sizeof(erase));
            erase.request_id = ev.value;
            ioctl(p->ui_fd, UI_BEGIN_FF_ERASE, &erase);
            ioctl(p->ui_fd, UI_END_FF_ERASE, &erase);
            LOGI("[%s] FF 擦除 id=%d → 发送归零", type_uname(p->type), erase.effect_id);
            rumble_send(p, 0, 0);
            p->stop_at = 0;
        } else if (ev.type == EV_FF) {
            int id = ev.code;
            if (id >= 0 && id < MAX_EFFECTS && ev.value == 1) {
                struct ff_effect *fe = &p->effects[id];
                int s = fe->u.rumble.strong_magnitude * 100 / 0xffff;
                int w = fe->u.rumble.weak_magnitude * 100 / 0xffff;
                if (fe->type != FF_RUMBLE) { s = w = 60; }
                int dur = fe->replay.length <= 0 ? 400 : fe->replay.length;
                if (dur > 5000) dur = 5000;
                /* 流式合并：强度不变不重发；段间不发归零，仅流结束(+150ms宽限)才停 */
                if (s != p->last_s || w != p->last_w) {
                    LOGT("[%s] FF 播放 id=%d → 强=%d 弱=%d %dms", type_uname(p->type), id, s, w, dur);
                    p->want_s = s; p->want_w = w; p->dirty = 1;
                    pad_flush(p);
                }
                stream_extend(p, dur);
            } else if (id >= 0 && ev.value == 0) {
                LOGT("[%s] FF 停止 id=%d", type_uname(p->type), id);
                if (rumble_send(p, 0, 0) < 0) {
                    p->need_zero = 1;              /* 归零失败，主循环补发 */
                    LOGT("[%s] 归零被挂起(need_zero)", type_uname(p->type));
                }
                p->stop_at = 0;
                p->last_s = p->last_w = -1;
                p->dirty = 0;
                p->last_send = now_ms();
            }
        }
    }
}

/* 热插拔：扫描并挂载新接入的已支持手柄（按节点编号去重） */
static void try_attach_new(void) {
    char path[32];
    int free_slot = -1;
    for (int s = 0; s < MAX_PADS; s++) if (!pads[s].type) { free_slot = s; break; }
    if (free_slot < 0) return;

    for (int i = 0; i < 16; i++) {
        int dup = 0;
        for (int q = 0; q < MAX_PADS; q++) if (pads[q].hid_idx == i) dup = 1;
        if (dup) continue;
        snprintf(path, sizeof(path), "/dev/hidraw%d", i);
        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) { close(fd); continue; }
        int t = classify(info.bustype, (uint16_t)info.vendor, (uint16_t)info.product);
        if (!t) { close(fd); continue; }
        /* 找匹配且未占用的 evdev */
        int done = 0;
        for (int e = 0; e < 64 && !done; e++) {
            int edup = 0;
            for (int q = 0; q < MAX_PADS; q++) if (pads[q].ev_idx == e) edup = 1;
            if (edup) continue;
            snprintf(path, sizeof(path), "/dev/input/event%d", e);
            int ef = open(path, O_RDONLY | O_NONBLOCK);
            if (ef < 0) continue;
            struct input_id id;
            if (ioctl(ef, EVIOCGID, &id) < 0 || classify(id.bustype, id.vendor, id.product) != t) {
                close(ef); continue;
            }
            char nm[80] = {0};
            ioctl(ef, EVIOCGNAME(sizeof(nm) - 1), nm);
            if (strstr(nm, "Rumble")) { close(ef); continue; }   /* 虚拟手柄不代理 */
            if (ioctl(ef, EVIOCGRAB, 1) < 0) {
                /* grab 失败视为重复/被占，放弃该组合 */
                close(ef);
                continue;
            }
            ioctl(ef, EVIOCGNAME(sizeof(pads[free_slot].ev_name) - 1), pads[free_slot].ev_name);
            pads[free_slot].type = t;
            pads[free_slot].hid_fd = fd;
            pads[free_slot].hid_idx = i;
            pads[free_slot].ev_fd = ef;
            pads[free_slot].ev_idx = e;
            reset_pad_link(&pads[free_slot]);
            pads[free_slot].ui_fd = create_uinput(&pads[free_slot]);
            if (pads[free_slot].ui_fd < 0) {
                LOGE("uinput 创建失败，回滚 hidraw%d/event%d", i, e);
                close(ef); close(fd);
                memset(&pads[free_slot], 0, sizeof(pads[free_slot]));
                pads[free_slot].hid_fd = pads[free_slot].ev_fd = pads[free_slot].ui_fd = -1;
                pads[free_slot].hid_idx = pads[free_slot].ev_idx = -1;
            } else {
                hide_node(&pads[free_slot]);
            }
            done = 1;
        }
        if (!done) close(fd);   /* 该 hidraw 没找到可用 evdev */
        return;                  /* 每次调用只挂载一只 */
    }
}

/* ---------------- 开关：标志文件存在=暂停驱动（释放独占，设备回归直连） ---------------- */

static const char *OFF_FLAGS[] = {
    "/sdcard/rumble_driver_off",
    "/sdcard/Android/data/com.ukeeeen.gamepadrumbletest/files/rumble_off",
};

/* 直通命令：App 写 rumble_cmd 文件（"<S|G|A> <强> <弱> <ms>"），驱动直接发hidraw，
 * 绕过框架震动会话队列（测试模式的精确时序通道）。读完即删。 */
static const char *CMD_FILE = "/sdcard/Android/data/com.ukeeeen.gamepadrumbletest/files/rumble_cmd";

static void stream_extend(struct pad *p, int dur) {
    /* 意图时间轴：结束=锚点+累计时长。队列滞后倾泻事件不延长结束时刻，到点即停 */
    int64_t nowp = now_ms();
    if (nowp < p->stream_dead) return;   /* 流已到期：丢弃迟到的散包 */
    if (p->stream_pos == 0 || nowp - p->stream_last_ev > 500) {
        p->stream_anchor = nowp;
        p->stream_pos = 0;
    }
    p->stream_last_ev = nowp;
    p->stream_pos += dur;
    p->stop_at = p->stream_anchor + p->stream_pos + 150;
}

static void dispatch_cmd(const char *which, int s, int w, int dur) {
    if (which[0] == 0) return;
    /* 日志节流：强度变化或≥500ms才记，防200Hz流刷爆日志 */
    static int64_t last_log = 0;
    static int ls = -9, lw = -9;
    int64_t nowlog = now_ms();
    if (abs(s - ls) >= 5 || abs(w - lw) >= 5 || s == 0 || nowlog - last_log > 500) {
        LOGT("[直通] %s 强=%d 弱=%d %dms", which, s, w, dur);
        ls = s; lw = w; last_log = nowlog;
    }
    for (int i = 0; i < MAX_PADS; i++) {
        if (!pads[i].type) continue;
        int hit = (which[0] == 'A') ||
                  (which[0] == 'S' && pads[i].type == PAD_STADIA) ||
                  (which[0] == 'G' && (pads[i].type == PAD_GAMESIR_X || pads[i].type == PAD_GAMESIR_ANDROID));
        if (!hit) continue;
        if (dur > 0) {
            if (dur > 10000) dur = 10000;
            /* 走限速通道：与FF路径同一套自适应节奏+噪声门，绝不裸写hidraw */
            pads[i].want_s = s; pads[i].want_w = w; pads[i].dirty = 1;
            stream_extend(&pads[i], dur);
            pads[i].need_zero = 0;      /* 新命令作废挂起归零 */
            pad_flush(&pads[i]);
        } else {
            rumble_send(&pads[i], 0, 0);
            pads[i].last_s = pads[i].last_w = -1;
            pads[i].dirty = 0;
            pads[i].stop_at = 0;
            pads[i].stream_pos = 0;     /* 显式停止终结当前流 */
        }
    }
}

static void poll_cmd(void) {
    FILE *f = fopen(CMD_FILE, "r");
    if (!f) return;
    char which[8] = {0};
    int s = 0, w = 0, dur = 0;
    int got = (fscanf(f, "%7s %d %d %d", which, &s, &w, &dur) == 4);
    fclose(f);
    unlink(CMD_FILE);
    if (got) dispatch_cmd(which, s, w, dur);
}

/* ---------------- 模块简介动态状态（回写 module.prop 的 description 行） ---------------- */

#define MODULE_PROP "/data/adb/modules/rumble_drivers/module.prop"

static void update_module_status(int paused_now) {
    static int64_t last = 0;
    static int last_paused = -1;
    int64_t now = now_ms();
    int state_changed = (paused_now != last_paused);
    if (!state_changed && last && now - last < 5000) return;
    last = now;
    last_paused = paused_now;

    char desc[300];
    if (paused_now) {
        snprintf(desc, sizeof(desc), "状态：已暂停（手柄直连，无震动）｜点「执行」按钮恢复隐藏+震动");
    } else {
        int total = 0, hidden_n = 0;
        char names[96] = "";
        for (int i = 0; i < MAX_PADS; i++) {
            if (!pads[i].type) continue;
            total++;
            if (pads[i].hidden) {
                hidden_n++;
                const char *sn = (pads[i].type == PAD_STADIA) ? "Stadia" : "小鸡";
                if (names[0]) strncat(names, "·", sizeof(names) - strlen(names) - 1);
                strncat(names, sn, sizeof(names) - strlen(names) - 1);
            }
        }
        if (total == 0)
            snprintf(desc, sizeof(desc), "状态：运行中，等待手柄连接（连接后自动代理并隐藏）");
        else if (hidden_n == 0)
            snprintf(desc, sizeof(desc), "状态：运行中 · %d 只手柄已代理（原设备未隐藏）", total);
        else
            snprintf(desc, sizeof(desc), "状态：运行中 · %d 只已代理 · 已隐藏：%s", total, names);
    }

    /* 原子重写 module.prop：仅替换 description 行，其余保留 */
    FILE *f = fopen(MODULE_PROP, "r");
    if (!f) return;
    char buf[2048], out[2048];
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[r] = 0;
    out[0] = 0;
    char *line = buf, *nl;
    int wrote = 0;
    while ((nl = strchr(line, '\n')) != NULL) {
        *nl = 0;
        if (strncmp(line, "description=", 12) == 0) {
            snprintf(out + strlen(out), sizeof(out) - strlen(out) - 1, "description=%s\n", desc);
            wrote = 1;
        } else {
            snprintf(out + strlen(out), sizeof(out) - strlen(out) - 1, "%s\n", line);
        }
        line = nl + 1;
    }
    if (*line) {
        if (strncmp(line, "description=", 12) == 0) {
            snprintf(out + strlen(out), sizeof(out) - strlen(out) - 1, "description=%s\n", desc);
            wrote = 1;
        } else {
            snprintf(out + strlen(out), sizeof(out) - strlen(out) - 1, "%s\n", line);
        }
    }
    if (!wrote)
        snprintf(out + strlen(out), sizeof(out) - strlen(out) - 1, "description=%s\n", desc);
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", MODULE_PROP);
    FILE *o = fopen(tmp, "w");
    if (!o) return;
    fputs(out, o);
    fclose(o);
    rename(tmp, MODULE_PROP);
}

/* ---------------- 直通 socket 服务端（抽象域 @rumble_driver，任何App进程可连） ---------------- */

#define SOCK_NAME "rumble_driver"
#define SOCK_CLIENTS 4
static int sock_srv = -1;
static int sock_cli[SOCK_CLIENTS];
static char sock_buf[SOCK_CLIENTS][256];
static int sock_len[SOCK_CLIENTS];

static void sock_init(void) {
    for (int i = 0; i < SOCK_CLIENTS; i++) { sock_cli[i] = -1; sock_len[i] = 0; }
    sock_srv = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_srv < 0) return;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strcpy(addr.sun_path + 1, SOCK_NAME);
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(SOCK_NAME));
    if (bind(sock_srv, (struct sockaddr *)&addr, len) < 0 || listen(sock_srv, 4) < 0) {
        LOGE("socket监听失败: %s", strerror(errno));
        close(sock_srv); sock_srv = -1; return;
    }
    LOGI("直通socket已监听: @%s", SOCK_NAME);
}

static void sock_handle(int idx) {
    char buf[256];
    LOGT("[sock] 客户端%d 数据到达", idx);
    ssize_t r = read(sock_cli[idx], buf, sizeof(buf) - 1);
    if (r <= 0) { close(sock_cli[idx]); sock_cli[idx] = -1; sock_len[idx] = 0; return; }
    buf[r] = 0;
    /* 拼接半包，按行分发 */
    int keep = sock_len[idx];
    if (keep + r >= (int)sizeof(sock_buf[idx])) keep = 0;   // 溢出保护：重置
    memcpy(sock_buf[idx] + keep, buf, r);
    sock_len[idx] = keep + r;
    sock_buf[idx][sock_len[idx]] = 0;
    char *line = sock_buf[idx], *nl;
    while ((nl = strchr(line, '\n')) != NULL) {
        *nl = 0;
        char which[8] = {0}; int s = 0, w = 0, dur = 0;
        if (sscanf(line, "%7s %d %d %d", which, &s, &w, &dur) == 4)
            dispatch_cmd(which, s, w, dur);
        line = nl + 1;
    }
    sock_len[idx] = strlen(line);
    memmove(sock_buf[idx], line, sock_len[idx]);
}
static int driver_disabled(void) {
    for (size_t i = 0; i < sizeof(OFF_FLAGS) / sizeof(OFF_FLAGS[0]); i++) {
        int fd = open(OFF_FLAGS[i], O_RDONLY);
        if (fd >= 0) { close(fd); return 1; }
    }
    return 0;
}

static void teardown_all(void) {
    for (int i = 0; i < MAX_PADS; i++) {
        if (!pads[i].type) continue;
        if (pads[i].hidden) unhide_node(&pads[i]);
        if (pads[i].ui_fd >= 0) { ioctl(pads[i].ui_fd, UI_DEV_DESTROY); close(pads[i].ui_fd); }
        if (pads[i].ev_fd >= 0) close(pads[i].ev_fd);   /* 关闭即释放 EVIOCGRAB */
        if (pads[i].hid_fd >= 0) close(pads[i].hid_fd);
        LOGI("已释放: %s", pads[i].ev_name[0] ? pads[i].ev_name : type_uname(pads[i].type));
    }
    memset(pads, 0, sizeof(pads));
    for (int i = 0; i < MAX_PADS; i++) {
        pads[i].hid_fd = pads[i].ev_fd = pads[i].ui_fd = -1;
        pads[i].hid_idx = pads[i].ev_idx = -1;
        reset_pad_link(&pads[i]);
    }
}

/* 全量挂载：discover + 独占 + uinput（仅在无活动手柄时调用） */
static int attach_all(void) {
    if (discover() == 0) return 0;
    int active = 0;
    for (int i = 0; i < MAX_PADS; i++) {
        if (!pads[i].type) continue;
        if (ioctl(pads[i].ev_fd, EVIOCGRAB, 1) < 0) {
            LOGE("EVIOCGRAB 失败(跳过该槽): %s", strerror(errno));
            close(pads[i].ev_fd); close(pads[i].hid_fd);
            pads[i].ev_fd = pads[i].hid_fd = -1;
            pads[i].ev_idx = pads[i].hid_idx = -1;
            pads[i].type = 0;
            continue;
        }
        pads[i].ui_fd = create_uinput(&pads[i]);
        if (pads[i].ui_fd < 0) { pads[i].type = 0; continue; }
        hide_node(&pads[i]);
        active++;
    }
    return active;
}

static int mode_uinput(void) {
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal);
    unhide_stale_at_boot();
    sock_init();
    int disabled_state = 0;
    int64_t last_scan = now_ms();

    for (;;) {
        /* 开关检测（≤2秒生效） */
        int off = driver_disabled();
        if (off != disabled_state) {
            if (off) { teardown_all(); LOGI("开关→关：驱动暂停，手柄回归系统直连（无震动）"); }
            else     { LOGI("开关→开：驱动恢复"); }
            disabled_state = off;
        }
        if (disabled_state) { update_module_status(1); sleep(2); continue; }

        int active = 0;
        for (int i = 0; i < MAX_PADS; i++) if (pads[i].type) active++;
        if (active == 0) {
            attach_all();
            for (int i = 0; i < MAX_PADS; i++) if (pads[i].type) active++;
            if (active == 0) { sleep(3); continue; }   /* 没有手柄，稍后重扫 */
        }

        struct pollfd pf[MAX_PADS * 2 + 1 + SOCK_CLIENTS];
        struct pad *map[MAX_PADS * 2 + 1 + SOCK_CLIENTS];
        int n = 0;
        int64_t soonest = 0;
        for (int i = 0; i < MAX_PADS; i++) {
            if (!pads[i].type) continue;
            pf[n].fd = pads[i].ev_fd; pf[n].events = POLLIN; pf[n].revents = 0; map[n] = &pads[i]; n++;
            pf[n].fd = pads[i].ui_fd; pf[n].events = POLLIN; pf[n].revents = 0; map[n] = &pads[i]; n++;
            if (pads[i].stop_at > 0 && (soonest == 0 || pads[i].stop_at < soonest))
                soonest = pads[i].stop_at;
        }
        /* 直通socket：服务端+已连客户端 */
        int sock_slot[1 + SOCK_CLIENTS];
        int sock_n = 0;
        if (sock_srv >= 0) {
            pf[n].fd = sock_srv; pf[n].events = POLLIN; pf[n].revents = 0; map[n] = NULL; n++;
            sock_slot[sock_n++] = -1;
        }
        for (int i = 0; i < SOCK_CLIENTS; i++) {
            if (sock_cli[i] < 0) continue;
            pf[n].fd = sock_cli[i]; pf[n].events = POLLIN; pf[n].revents = 0; map[n] = NULL; n++;
            sock_slot[sock_n++] = i;
        }
        int timeout = -1;
        if (soonest > 0) { timeout = (int)(soonest - now_ms()); if (timeout < 0) timeout = 0; }
        if (timeout < 0 || timeout > 50) timeout = 50;   /* 50ms：直通命令/停止的响应粒度 */
        /* 有待发振幅/挂起归零时更快唤醒 */
        for (int i = 0; i < MAX_PADS; i++)
            if (pads[i].type && (pads[i].dirty || pads[i].need_zero) && timeout > 25) timeout = 25;
        poll(pf, n, timeout);

        int64_t now = now_ms();
        poll_cmd();                                     /* 直通命令通道 */
        update_module_status(disabled_state);           /* 模块简介实时状态 */
        if (now - last_scan > 3000) { try_attach_new(); last_scan = now; }

        int broken = 0;
        for (int k = 0; k < n; k++) {
            if (pf[k].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (map[k] == NULL) {              /* socket：先排水缓冲数据再关闭 */
                    int ci = sock_slot[k - (n - sock_n)];
                    if (ci == -1) { broken = 1; break; }   /* 服务端异常 */
                    if (pf[k].revents & POLLIN) sock_handle(ci);
                    close(sock_cli[ci]); sock_cli[ci] = -1; sock_len[ci] = 0;
                    continue;
                }
                broken = 1; break;
            }
            if (!(pf[k].revents & POLLIN)) continue;
            if (map[k] == NULL) {                  /* socket事件 */
                if (pf[k].fd == sock_srv) {
                    int c = accept(sock_srv, NULL, NULL);
                    LOGT("[sock] accept=%d", c);
                    if (c >= 0) {
                        int slot = -1;
                        for (int i = 0; i < SOCK_CLIENTS; i++) if (sock_cli[i] < 0) { slot = i; break; }
                        if (slot >= 0) { sock_cli[slot] = c; sock_len[slot] = 0; }
                        else close(c);
                    }
                } else {
                    for (int i = 0; i < SOCK_CLIENTS; i++)
                        if (sock_cli[i] == pf[k].fd) { sock_handle(i); break; }
                }
                continue;
            }
            struct pad *p = map[k];
            if (pf[k].fd == p->ev_fd) {
                struct input_event ev;
                while (read(p->ev_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                    if (ev.type == EV_KEY || ev.type == EV_ABS || ev.type == EV_SYN)
                        write(p->ui_fd, &ev, sizeof(ev));
                }
            } else {
                handle_uinput_read(p);
            }
        }
        if (broken) { LOGE("节点异常，重建代理"); teardown_all(); sleep(1); continue; }

        for (int i = 0; i < MAX_PADS; i++)
            if (pads[i].type && pads[i].stop_at > 0 && now >= pads[i].stop_at) {
                LOGT("[%s] 意图时间轴到期 → 归零（静默700ms丢弃迟到事件）", type_uname(pads[i].type));
                if (rumble_send(&pads[i], 0, 0) < 0) pads[i].need_zero = 1;
                pads[i].stop_at = 0;
                pads[i].last_s = pads[i].last_w = -1;
                pads[i].dirty = 0;
                pads[i].stream_pos = 0;
                pads[i].stream_dead = now + 700;
                pads[i].last_send = now;
            }
        /* 限速窗口到点后补发待发振幅 */
        for (int i = 0; i < MAX_PADS; i++)
            if (pads[i].type) pad_flush(&pads[i]);
        /* 归零兜底：EAGAIN丢包后每25ms补发直到成功 */
        for (int i = 0; i < MAX_PADS; i++)
            if (pads[i].type && pads[i].need_zero && now - pads[i].last_send >= MIN_SEND_MS) {
                if (rumble_send(&pads[i], 0, 0) == 0) {
                    pads[i].need_zero = 0;
                    pads[i].last_send = now;
                    LOGT("[%s] 补发归零成功", type_uname(pads[i].type));
                }
            }
    }
}

/* ---------------- main ---------------- */

int main(int argc, char **argv) {
    if (argc < 2) goto usage;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--layout") && i + 1 < argc) report_layout = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "gamesir")) pad_filter = PAD_GAMESIR_X | PAD_GAMESIR_ANDROID;
            else if (!strcmp(v, "stadia")) pad_filter = PAD_STADIA;
            else pad_filter = PAD_GAMESIR_X | PAD_GAMESIR_ANDROID | PAD_STADIA;
            LOGI("类型过滤: %s (mask=0x%x)", v, pad_filter);
        }
    }
    if (!strcmp(argv[1], "--info"))    return mode_info();
    if (!strcmp(argv[1], "--probe"))   return mode_probe();
    if (!strcmp(argv[1], "--uinput"))  return mode_uinput();
    if (!strcmp(argv[1], "--tap"))     return mode_tap(argc > 2 ? atoi(argv[2]) : 70, argc > 3 ? atoi(argv[3]) : 180);
    if (!strcmp(argv[1], "--oneshot") && argc >= 5)
        return mode_oneshot(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]));
usage:
    fprintf(stderr,
        "用法: %s --info | --oneshot S W MS | --probe | --tap [S] [MS] | --uinput [--layout N]\n",
        argv[0]);
    return 1;
}
