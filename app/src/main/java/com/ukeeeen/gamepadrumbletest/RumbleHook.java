package com.ukeeeen.gamepadrumbletest;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.VibrationEffect;
import android.view.InputDevice;

import java.io.OutputStream;
import java.lang.reflect.Field;
import java.util.concurrent.atomic.AtomicInteger;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage.LoadPackageParam;

/**
 * LSPosed 模块：拦截游戏的 Vibrator 调用，改走 rumble_driver 直通socket，
 * 绕过框架震动会话队列（低延迟）。在 LSPosed 中勾选目标应用（如 Eden）生效。
 */
public class RumbleHook implements IXposedHookLoadPackage {

    private static LocalSocket sock = null;
    private static OutputStream out = null;
    private static final Object sockLock = new Object();
    private static final AtomicInteger generation = new AtomicInteger(0);

    @Override
    public void handleLoadPackage(final LoadPackageParam lp) throws Throwable {
        if (lp.packageName == null) return;
        if (lp.packageName.equals("com.ukeeeen.gamepadrumbletest")) return;   // 自身保留框架测试
        if (lp.packageName.equals("android") || lp.packageName.startsWith("com.android.")) return;

        XC_MethodHook vibHook = new XC_MethodHook() {
            @Override
            protected void beforeHookedMethod(MethodHookParam p) throws Throwable {
                try { handleVibrate(p.thisObject, p.args); } catch (Throwable ignored) { }
                p.setResult(null);          // 吞掉原始调用，框架不再参与
            }
        };
        try {
            Class<?> ve = VibrationEffect.class;
            Class<?> va = Class.forName("android.os.VibrationAttributes");
            XposedHelpers.findAndHookMethod(android.os.Vibrator.class, "vibrate", ve, vibHook);
            XposedHelpers.findAndHookMethod(android.os.Vibrator.class, "vibrate", ve, va, vibHook);
        } catch (Throwable ignored) { }
        try {
            Class<?> vm = Class.forName("android.view.InputDevice$VibratorManager");
            XposedHelpers.findAndHookMethod(vm, "vibrate", VibrationEffect.class, vibHook);
            XposedHelpers.findAndHookMethod(vm, "vibrate", int.class, VibrationEffect.class, vibHook);
        } catch (Throwable ignored) { }
        try {
            XC_MethodHook cancelHook = new XC_MethodHook() {
                @Override
                protected void beforeHookedMethod(MethodHookParam p) throws Throwable {
                    generation.incrementAndGet();   // 作废进行中的模式
                    send(whichOf(p.thisObject), 0, 0, 0);
                    p.setResult(null);
                }
            };
            XposedHelpers.findAndHookMethod(android.os.Vibrator.class, "cancel", cancelHook);
        } catch (Throwable ignored) { }
    }

    /** 依据 thisObject 反查 deviceId → 设备名 → S/G/A */
    private static String whichOf(Object thiz) {
        try {
            Field f = null;
            for (Class<?> c = thiz.getClass(); c != null; c = c.getSuperclass()) {
                try { f = c.getDeclaredField("mDeviceId"); break; } catch (NoSuchFieldException e) { }
            }
            if (f == null) return "A";
            f.setAccessible(true);
            int id = f.getInt(thiz);
            InputDevice dev = InputDevice.getDevice(id);
            if (dev != null) {
                String n = dev.getName();
                if (n != null && n.contains("Stadia")) return "S";
                if (n != null && (n.contains("GameSir") || n.contains("Xbox") || n.contains("Nova"))) return "G";
            }
        } catch (Throwable ignored) { }
        return "A";
    }

    private static void handleVibrate(Object thiz, Object[] args) {
        VibrationEffect eff = null;
        if (args != null) for (Object a : args) if (a instanceof VibrationEffect) eff = (VibrationEffect) a;
        if (eff == null) return;
        final String which = whichOf(thiz);
        final int gen = generation.incrementAndGet();

        long[] timings = null;
        int[] amps = null;
        try {
            timings = (long[]) XposedHelpers.getObjectField(eff, "mTimings");
            amps = (int[]) XposedHelpers.getObjectField(eff, "mAmplitudes");
        } catch (Throwable ignored) { }

        if (timings != null && amps != null && timings.length > 2) {
            // 多步 Waveform：后台线程按步直发（游戏高频流基本不走这里）
            final long[] ts = timings; final int[] as = amps;
            Thread t = new Thread(() -> {
                for (int i = 0; i < ts.length && generation.get() == gen; i++) {
                    long dur = ts[i];
                    if (dur <= 0) continue;
                    int pct = as[i] < 0 ? 80 : as[i] * 100 / 255;
                    send(which, pct, pct, (int) Math.min(dur, 10000));
                    try { Thread.sleep(dur); } catch (InterruptedException e) { return; }
                }
            });
            t.setDaemon(true);
            t.start();
        } else if (timings != null && amps != null && timings.length > 0) {
            // 短波形（≤2步）：取最大振幅+总时长，单发等效（不启线程）
            long total = 0; int maxAmp = 0;
            for (int i = 0; i < timings.length; i++) {
                total += timings[i];
                if (amps[i] > maxAmp) maxAmp = amps[i];
            }
            int pct = maxAmp <= 0 ? 80 : maxAmp * 100 / 255;
            send(which, pct, pct, (int) Math.min(total, 10000));
        } else {
            // OneShot / 其他：反射取时长与振幅
            try {
                long dur = XposedHelpers.getLongField(eff, "mDuration");
                int amp = XposedHelpers.getIntField(eff, "mAmplitude");
                int pct = amp < 0 ? 80 : amp * 100 / 255;
                send(which, pct, pct, (int) Math.min(dur, 10000));
            } catch (Throwable ignored) {
                send(which, 60, 60, 150);
            }
        }
    }

    static void send(String which, int s, int w, int dur) {
        try {
            synchronized (sockLock) {
                if (out == null) {
                    sock = new LocalSocket();
                    sock.connect(new LocalSocketAddress("rumble_driver", LocalSocketAddress.Namespace.ABSTRACT));
                    out = sock.getOutputStream();
                }
            }
            String line = which + " " + s + " " + w + " " + dur + "\n";
            synchronized (sockLock) {
                out.write(line.getBytes());
                out.flush();
            }
        } catch (Throwable t) {
            try { if (sock != null) sock.close(); } catch (Throwable ignored) { }
            sock = null; out = null;
        }
    }
}
