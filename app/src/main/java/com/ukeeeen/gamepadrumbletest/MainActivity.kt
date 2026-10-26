package com.ukeeeen.gamepadrumbletest

import android.hardware.input.InputManager
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.os.VibrationEffect
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : ComponentActivity() {

    companion object {
        private const val TAG = "GamepadRumbleTest"
        private const val LOG_LIMIT = 200
    }

    private var devices by mutableStateOf<List<InputDevice>>(emptyList())
    private val eventLog = mutableStateListOf<String>()
    private val vizButtons = mutableStateMapOf<Int, Boolean>()
    private val vizAxes = mutableStateMapOf<Int, Float>()
    private val logFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    private val axisLastLogged = HashMap<Int, Long>()

    // 震动时序测试状态
    private var timingStatus by mutableStateOf("idle")   // idle/running/waiting/done
    private var timingResult by mutableStateOf("")
    private var timingRequestEnd = 0L
    private var timingDevice = ""

    // 图库通道：direct=直通(测驱动+蓝牙链路) / framework=框架(测全链路)
    private var patternChannel by mutableStateOf("direct")

    private val inputManager: InputManager by lazy { getSystemService(INPUT_SERVICE) as InputManager }

    private val deviceListener = object : InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) {
            log("＋ 输入设备已连接: ${nameOf(deviceId)}")
            refreshDevices()
        }

        override fun onInputDeviceRemoved(deviceId: Int) {
            log("－ 输入设备已断开: id=$deviceId")
            refreshDevices()
        }

        override fun onInputDeviceChanged(deviceId: Int) {
            log("＊ 输入设备已变化: ${nameOf(deviceId)}")
            refreshDevices()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { App() }
    }

    override fun onResume() {
        super.onResume()
        inputManager.registerInputDeviceListener(deviceListener, null)
        refreshDevices()
    }

    override fun onPause() {
        super.onPause()
        inputManager.unregisterInputDeviceListener(deviceListener)
    }

    // ---------- 设备枚举与描述 ----------

    private fun nameOf(id: Int): String = InputDevice.getDevice(id)?.name ?: "id=$id"

    private fun refreshDevices() {
        val list = InputDevice.getDeviceIds().toList()
            .mapNotNull { InputDevice.getDevice(it) }
            .filter { isGamepadLike(it) }          // 只显示手柄类设备
            .sortedBy { it.name }
        devices = list
        list.forEach { Log.i(TAG, describe(it)) }
        log("手柄设备 ${list.size} 个：${list.joinToString("、") { it.name }}")
    }

    private fun isGamepadLike(d: InputDevice): Boolean {
        val s = d.sources
        return (s and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (s and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    private fun describe(d: InputDevice): String {
        val vm = d.vibratorManager
        return buildString {
            append("device name=${d.name} id=${d.id} sources=0x%08X".format(Locale.US, d.sources))
            append(" descriptor=${d.descriptor}")
            if (Build.VERSION.SDK_INT >= 34) {
                append(" vid=%04X pid=%04X".format(Locale.US, d.vendorId, d.productId))
            }
            append(" vibrators=${vm.vibratorIds.size} hasVibrator=${vm.defaultVibrator.hasVibrator()}")
        }
    }

    private fun vidPid(d: InputDevice): String =
        if (Build.VERSION.SDK_INT >= 34) {
            "VID=%04X PID=%04X".format(Locale.US, d.vendorId, d.productId)
        } else {
            "VID/PID 需 Android 14+"
        }

    private fun sourceNames(d: InputDevice): String {
        val s = d.sources
        val names = mutableListOf<String>()
        if ((s and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) names += "GAMEPAD"
        if ((s and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) names += "JOYSTICK"
        if ((s and InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD) names += "DPAD"
        if ((s and InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD) names += "KEYBOARD"
        if ((s and InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN) names += "TOUCHSCREEN"
        if ((s and InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) names += "MOUSE"
        if ((s and InputDevice.SOURCE_TOUCHPAD) == InputDevice.SOURCE_TOUCHPAD) names += "TOUCHPAD"
        if ((s and InputDevice.SOURCE_TRACKBALL) == InputDevice.SOURCE_TRACKBALL) names += "TRACKBALL"
        if (names.isEmpty()) names += "OTHER"
        return names.joinToString("+") + " (0x%08X)".format(Locale.US, s)
    }

    // ---------- 震动 ----------

    private fun effectShort(): VibrationEffect = VibrationEffect.createOneShot(200, 255)

    private fun effectLong(): VibrationEffect = VibrationEffect.createOneShot(1000, 255)

    private fun effectRamp(): VibrationEffect = VibrationEffect.createWaveform(
        longArrayOf(0, 100, 100, 100, 100, 100, 100, 100),
        intArrayOf(0, 36, 72, 108, 144, 180, 216, 255),
        -1
    )

    private fun effectPulse(): VibrationEffect = VibrationEffect.createWaveform(
        longArrayOf(0, 60, 70, 60, 70, 60),
        -1
    )

    private fun rumble(d: InputDevice, label: String, effect: () -> VibrationEffect) {
        val v = d.vibratorManager.defaultVibrator
        if (v.hasVibrator()) {
            v.vibrate(effect())
            log("→ [$label] 已向「${d.name}」下发震动效果")
            Log.i(TAG, "rumble sent: device=${d.name} effect=$label")
        } else {
            log("✗ [${d.name}] hasVibrator=false，系统认为该设备不可震动")
            Log.w(TAG, "rumble refused: device=${d.name} hasVibrator=false")
        }
    }

    // ---------- 震动图库（压力测试模式集） ----------
    @Volatile private var patternStop = false
    private var patternThread: Thread? = null

    private fun stopPattern(d: InputDevice) {
        patternStop = true
        writeCmd(d, 0, 0, 0)
        d.vibratorManager.defaultVibrator.cancel()
        log("■ 已停止「${d.name}」的震动")
    }

    // 直通命令：写入 rumble_cmd，root 驱动直读直发，绕过框架震动队列（时序精确）
    private fun writeCmd(d: InputDevice, s: Int, w: Int, dur: Int) {
        val prefix = if (d.name.contains("Stadia", true)) "S" else "G"
        try {
            File(getExternalFilesDir(null) ?: filesDir, "rumble_cmd").writeText("$prefix $s $w $dur")
        } catch (_: Exception) { }
    }

    private fun playPattern(d: InputDevice, name: String) {
        val vib = d.vibratorManager.defaultVibrator
        if (!vib.hasVibrator()) { log("✗ ${d.name} 不可震动"); return }
        patternStop = true          // 旧模式线程自行退出，主线程不等待（避免卡顿）
        patternThread = null
        patternStop = false
        log("▶ [$name] ${d.name}")
        patternThread = Thread {
            val direct = patternChannel == "direct"
            // step: 直通=写命令文件(驱动直发) / 框架=走系统vibrate(全链路)
            fun step(amp: Int, on: Long, off: Long = 0) {
                if (patternStop) return
                if (direct) writeCmd(d, amp, amp, on.toInt())
                else vib.vibrate(VibrationEffect.createOneShot(on, amp.coerceIn(1, 255)))
                if (off > 0 && !patternStop) SystemClock.sleep(off)
            }
            fun sleepChunked(total: Long) {
                var left = total
                while (left > 0 && !patternStop) {
                    SystemClock.sleep(minOf(left, 50)); left -= 50
                }
            }
            when (name) {
                "短震" -> { step(255, 200) }
                "长震" -> { step(255, 1000) }
                "渐强" -> for (a in 1..8) step(a * 30, 100)
                "渐弱" -> for (a in 8 downTo 1) step(a * 30, 100)
                "脉冲" -> repeat(4) { step(255, 60, 70) }
                "机关枪" -> repeat(20) { step(255, 30, 30) }
                "心跳" -> repeat(8) { step(255, 120, 60); step(200, 100, 450) }
                "波浪" -> {
                    val t0 = SystemClock.elapsedRealtime(); var i = 0
                    while (SystemClock.elapsedRealtime() - t0 < 2000 && !patternStop) {
                        step((128 + 120 * Math.sin(i / 6.0)).toInt().coerceIn(1, 255), 25, 10); i++
                    }
                }
                "锯齿" -> repeat(6) { for (a in 1..6) step(a * 40, 40) }
                "风暴" -> {
                    val t0 = SystemClock.elapsedRealtime()
                    while (SystemClock.elapsedRealtime() - t0 < 3000 && !patternStop) {
                        step((20 + (Math.random() * 236)).toInt().coerceIn(1, 255), 30, 15)
                    }
                }
                "极限10s" -> {
                    val t0 = SystemClock.elapsedRealtime()
                    while (SystemClock.elapsedRealtime() - t0 < 10000 && !patternStop) {
                        step((1 + (Math.random() * 255)).toInt().coerceIn(1, 255), 30, 10)
                    }
                }
            }
            writeCmd(d, 0, 0, 0)
            if (!patternStop) log("■ [$name·${if (direct) "直通" else "框架"}] ${d.name} 完成")
        }.also { it.start() }
    }

    private fun startTimingTest(d: InputDevice, mode: String) {
        val vib = d.vibratorManager.defaultVibrator
        if (!vib.hasVibrator()) { log("✗ ${d.name} 不可震动"); return }
        timingStatus = "running"; timingResult = ""
        log("⏱ [$mode] ${d.name} 测试开始")
        Thread {
            if (mode == "oneshot") {
                vib.vibrate(VibrationEffect.createOneShot(5000, 180))
                SystemClock.sleep(5000)
            } else {
                // 模拟 Eden/Switch：~150-200Hz 变振幅短段流
                val t0 = SystemClock.elapsedRealtime()
                var i = 0
                while (SystemClock.elapsedRealtime() - t0 < 3000) {
                    val amp = (128 + 100 * Math.sin(i / 8.0)).toInt().coerceIn(1, 255)
                    vib.vibrate(VibrationEffect.createOneShot(8, amp))
                    SystemClock.sleep(5)
                    i++
                }
            }
            timingRequestEnd = SystemClock.elapsedRealtime()
            timingDevice = d.name
            timingStatus = "waiting"
            log("⏱ [$mode] ${d.name} 请求已结束 —— 手柄停震后立刻点「停了」")
        }.start()
    }

    private fun timingTapStop() {
        if (timingStatus != "waiting") return
        val tail = SystemClock.elapsedRealtime() - timingRequestEnd
        timingStatus = "done"
        timingResult = "${timingDevice}: 请求结束 → 感知停止 = ${tail}ms"
        log("⏱ 结果 $timingResult")
    }

    // ---------- 输入事件捕获 ----------

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val d = event.device
        val action = if (event.action == KeyEvent.ACTION_DOWN) "DOWN" else "UP"
        log("KEY ${KeyEvent.keyCodeToString(event.keyCode)} $action  dev=${d?.name ?: "无设备"}")
        // 可视化状态：手柄按键 + 十字键
        val c = event.keyCode
        if (c in KeyEvent.KEYCODE_DPAD_UP..KeyEvent.KEYCODE_DPAD_RIGHT ||
            c in KeyEvent.KEYCODE_BUTTON_A..KeyEvent.KEYCODE_BUTTON_MODE
        ) {
            vizButtons[c] = event.action == KeyEvent.ACTION_DOWN
        }
        // 消费手柄专用按键，避免误触界面控件；方向键等系统键照常传递
        if (d != null && isGamepadLike(d) &&
            event.keyCode in KeyEvent.KEYCODE_BUTTON_A..KeyEvent.KEYCODE_BUTTON_MODE
        ) {
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.action == MotionEvent.ACTION_MOVE) {
            val d = event.device
            if (d != null && (d.sources and InputDevice.SOURCE_JOYSTICK) != 0) {
                val now = SystemClock.elapsedRealtime()
                d.motionRanges.forEach { r ->
                    val v = event.getAxisValue(r.axis)
                    vizAxes[r.axis] = v
                    if (kotlin.math.abs(v) > 0.25f && now - (axisLastLogged[r.axis] ?: 0L) > 250L) {
                        axisLastLogged[r.axis] = now
                        log("AXIS ${axisName(r.axis)}=${"%.2f".format(Locale.US, v)}  dev=${d.name}")
                    }
                }
                return true
            }
        }
        return super.dispatchGenericMotionEvent(event)
    }

    private fun axisName(axis: Int): String = when (axis) {
        MotionEvent.AXIS_X -> "X(左摇杆X)"
        MotionEvent.AXIS_Y -> "Y(左摇杆Y)"
        MotionEvent.AXIS_Z -> "Z(右摇杆X)"
        MotionEvent.AXIS_RZ -> "RZ(右摇杆Y)"
        MotionEvent.AXIS_RX -> "RX"
        MotionEvent.AXIS_RY -> "RY"
        MotionEvent.AXIS_HAT_X -> "HAT_X(十字键X)"
        MotionEvent.AXIS_HAT_Y -> "HAT_Y(十字键Y)"
        MotionEvent.AXIS_GAS -> "GAS(右扳机)"
        MotionEvent.AXIS_BRAKE -> "BRAKE(左扳机)"
        MotionEvent.AXIS_LTRIGGER -> "LTRIGGER(左扳机)"
        MotionEvent.AXIS_RTRIGGER -> "RTRIGGER(右扳机)"
        else -> "axis#$axis"
    }

    // ---------- 日志 ----------

    private fun log(msg: String) {
        val line = "${logFormat.format(Date())}  $msg"
        eventLog.add(0, line)
        while (eventLog.size > LOG_LIMIT) eventLog.removeAt(eventLog.size - 1)
        Log.d(TAG, msg)
    }

    // ---------- UI ----------

    @Composable
    private fun App() {
        MaterialTheme(colorScheme = darkColorScheme()) {
            Surface(modifier = Modifier.fillMaxSize()) {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .statusBarsPadding()
                        .navigationBarsPadding()
                        .verticalScroll(rememberScrollState())
                        .padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Text(
                        "手柄震动测试 v1.1 · 魅族21",
                        style = MaterialTheme.typography.titleMedium
                    )
                    Text(
                        "已连接手柄 ${devices.size} 个" + if (devices.isEmpty()) "（连接手柄后自动出现）" else "",
                        fontSize = 12.sp,
                        color = Color(0xFFAAAAAA)
                    )

                    GamepadVisualCard()
                    DriverSwitchCard()
                    RumbleTimingCard()
                    devices.forEach { d -> DeviceCard(d) }
                    EventLogCard()
                }
            }
        }
    }

    @Composable
    private fun GamepadVisualCard() {
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(10.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text("手柄可视化（实时）", fontSize = 15.sp, style = MaterialTheme.typography.titleSmall)
                val tm = rememberTextMeasurer()
                Canvas(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(260.dp)
                        .semantics { contentDescription = "viz_canvas" }
                ) {
                    val w = size.width
                    val h = size.height
                    val bg = Color(0xFF14171C)       // 底板：明显深于卡片背景
                    val idle = Color(0xFFAEB8C6)     // 未按下：亮银白（高反差）
                    val on = Color(0xFF39D98A)       // 按下：亮绿
                    val stick = Color(0xFFFFA028)    // 摇杆头：亮橙
                    val well = Color(0xFF0D0F13)     // 摇杆井
                    val label = Color(0xFF14171C)    // 按钮字母：深色（配亮色按键）
                    fun pressed(k: Int) = vizButtons[k] == true
                    fun axis(a: Int) = vizAxes[a] ?: 0f
                    fun face(k: Int) = if (pressed(k)) on else idle
                    val rr = CornerRadius(h * 0.02f, h * 0.02f)
                    val labelStyle = TextStyle(fontSize = (h * 0.045f).toSp(), color = label)

                    // 机身底板
                    drawRoundRect(bg, cornerRadius = CornerRadius(h * 0.05f, h * 0.05f), size = Size(w, h))

                    // 肩键（Row1 左右横条）
                    drawRoundRect(if (pressed(KeyEvent.KEYCODE_BUTTON_L1)) on else idle, Offset(w * 0.05f, h * 0.05f), Size(w * 0.10f, h * 0.07f), rr)
                    drawRoundRect(if (pressed(KeyEvent.KEYCODE_BUTTON_R1)) on else idle, Offset(w * 0.85f, h * 0.05f), Size(w * 0.10f, h * 0.07f), rr)
                    drawText(tm, "L1", Offset(w * 0.075f, h * 0.06f), style = labelStyle)
                    drawText(tm, "R1", Offset(w * 0.875f, h * 0.06f), style = labelStyle)

                    // 扳机（Row1 下方，模拟量填充）
                    val l2 = (axis(MotionEvent.AXIS_BRAKE).coerceIn(0f, 1f)).coerceAtLeast(if (pressed(KeyEvent.KEYCODE_BUTTON_L2)) 1f else 0f)
                    val r2 = (axis(MotionEvent.AXIS_GAS).coerceIn(0f, 1f)).coerceAtLeast(if (pressed(KeyEvent.KEYCODE_BUTTON_R2)) 1f else 0f)
                    drawRoundRect(idle, Offset(w * 0.05f, h * 0.15f), Size(w * 0.10f, h * 0.14f), rr)
                    if (l2 > 0.02f) drawRoundRect(on.copy(alpha = 0.4f + 0.6f * l2), Offset(w * 0.05f, h * 0.15f + h * 0.14f * (1f - l2)), Size(w * 0.10f, h * 0.14f * l2), rr)
                    drawRoundRect(idle, Offset(w * 0.85f, h * 0.15f), Size(w * 0.10f, h * 0.14f), rr)
                    if (r2 > 0.02f) drawRoundRect(on.copy(alpha = 0.4f + 0.6f * r2), Offset(w * 0.85f, h * 0.15f + h * 0.14f * (1f - r2)), Size(w * 0.10f, h * 0.14f * r2), rr)
                    drawText(tm, "L2", Offset(w * 0.075f, h * 0.155f), style = labelStyle)
                    drawText(tm, "R2", Offset(w * 0.875f, h * 0.155f), style = labelStyle)

                    // 中键：Select / Home / Start（Row1 中央）
                    drawCircle(if (pressed(KeyEvent.KEYCODE_BUTTON_SELECT)) on else idle, h * 0.035f, Offset(w * 0.43f, h * 0.12f))
                    drawCircle(if (pressed(KeyEvent.KEYCODE_BUTTON_MODE)) on else idle, h * 0.04f, Offset(w * 0.50f, h * 0.12f))
                    drawCircle(if (pressed(KeyEvent.KEYCODE_BUTTON_START)) on else idle, h * 0.035f, Offset(w * 0.57f, h * 0.12f))

                    // Row2：左摇杆（左）+ ABXY（右）
                    val lsR = h * 0.115f
                    val lsC = Offset(w * 0.24f, h * 0.42f)
                    drawCircle(if (pressed(KeyEvent.KEYCODE_BUTTON_THUMBL)) on else idle, lsR, lsC)
                    drawCircle(well, lsR * 0.64f, lsC)
                    drawCircle(
                        stick, lsR * 0.32f,
                        Offset(lsC.x + axis(MotionEvent.AXIS_X) * lsR * 0.56f,
                               lsC.y + axis(MotionEvent.AXIS_Y) * lsR * 0.56f)
                    )

                    val abC = Offset(w * 0.73f, h * 0.42f)
                    val off = h * 0.085f
                    val r = h * 0.048f
                    fun faceBtn(k: Int, c: Offset, s: String) {
                        drawCircle(face(k), r, c)
                        drawText(tm, s, Offset(c.x - h * 0.022f, c.y - h * 0.030f), style = labelStyle)
                    }
                    faceBtn(KeyEvent.KEYCODE_BUTTON_Y, Offset(abC.x, abC.y - off), "Y")
                    faceBtn(KeyEvent.KEYCODE_BUTTON_X, Offset(abC.x - off, abC.y), "X")
                    faceBtn(KeyEvent.KEYCODE_BUTTON_B, Offset(abC.x + off, abC.y), "B")
                    faceBtn(KeyEvent.KEYCODE_BUTTON_A, Offset(abC.x, abC.y + off), "A")

                    // Row3：十字键（左中）+ 右摇杆（右中）
                    // 十字键兼容两种协议：DPAD按键 和 HAT轴（Xbox协议手柄走轴）
                    val hatX = axis(MotionEvent.AXIS_HAT_X)
                    val hatY = axis(MotionEvent.AXIS_HAT_Y)
                    val dpU = pressed(KeyEvent.KEYCODE_DPAD_UP) || hatY < -0.5f
                    val dpD = pressed(KeyEvent.KEYCODE_DPAD_DOWN) || hatY > 0.5f
                    val dpL = pressed(KeyEvent.KEYCODE_DPAD_LEFT) || hatX < -0.5f
                    val dpR = pressed(KeyEvent.KEYCODE_DPAD_RIGHT) || hatX > 0.5f
                    val dpC = Offset(w * 0.35f, h * 0.78f)
                    val arm = h * 0.042f
                    val len = h * 0.085f
                    drawRoundRect(if (dpU) on else idle, Offset(dpC.x - arm / 2, dpC.y - len - arm / 2), Size(arm, len), rr)
                    drawRoundRect(if (dpD) on else idle, Offset(dpC.x - arm / 2, dpC.y + arm / 2), Size(arm, len), rr)
                    drawRoundRect(if (dpL) on else idle, Offset(dpC.x - len - arm / 2, dpC.y - arm / 2), Size(len, arm), rr)
                    drawRoundRect(if (dpR) on else idle, Offset(dpC.x + arm / 2, dpC.y - arm / 2), Size(len, arm), rr)

                    val rsC = Offset(w * 0.65f, h * 0.78f)
                    drawCircle(if (pressed(KeyEvent.KEYCODE_BUTTON_THUMBR)) on else idle, lsR, rsC)
                    drawCircle(well, lsR * 0.64f, rsC)
                    drawCircle(
                        stick, lsR * 0.32f,
                        Offset(rsC.x + axis(MotionEvent.AXIS_Z) * lsR * 0.56f,
                               rsC.y + axis(MotionEvent.AXIS_RZ) * lsR * 0.56f)
                    )
                }
                Text(
                    "按下按键/拨动摇杆即点亮；扳机条为模拟量。事件来源：当前系统路由的手柄。",
                    fontSize = 11.sp, color = Color(0xFF888888)
                )
            }
        }
    }

    @Composable
    private fun DriverSwitchCard() {
        val flag = remember {
            File(getExternalFilesDir(null) ?: filesDir, "rumble_off")
        }
        var paused by remember { mutableStateOf(flag.exists()) }
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(10.dp),
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                Text("驱动开关", fontSize = 15.sp, style = MaterialTheme.typography.titleSmall)
                Text(
                    if (paused) "⏸ 已暂停：手柄直连系统（无震动），虚拟手柄已移除"
                    else "▶ 运行中：虚拟手柄已启用，游戏内选择 Rumble 结尾的设备",
                    fontSize = 12.sp,
                    color = if (paused) Color(0xFFE0B050) else Color(0xFF81C784)
                )
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Button(onClick = {
                        paused = if (paused) {
                            flag.delete()
                            log("○ 驱动开关：已请求恢复（≤2秒生效）")
                            false
                        } else {
                            flag.createNewFile()
                            log("‖ 驱动开关：已请求暂停（≤2秒生效）")
                            true
                        }
                    }) { Text(if (paused) "恢复驱动（要震动）" else "暂停驱动（要直连）") }
                }
                Text(
                    "适用于只认第一个手柄的App：暂停驱动后手柄回归系统直连；要震动时再恢复。",
                    fontSize = 11.sp, color = Color(0xFF888888)
                )
            }
        }
    }

    @Composable
    private fun DeviceCard(d: InputDevice) {
        val vm = d.vibratorManager
        val has = vm.defaultVibrator.hasVibrator()
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(10.dp),
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(if (isGamepadLike(d)) "🎮" else "⌨", fontSize = 16.sp)
                    Text(d.name, fontSize = 15.sp, style = MaterialTheme.typography.titleSmall)
                }
                Text(
                    "id=${d.id}  ${vidPid(d)}",
                    fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Color(0xFFBBBBBB)
                )
                Text(
                    sourceNames(d),
                    fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Color(0xFFBBBBBB)
                )
                Text(
                    if (has) {
                        "✅ 系统报告可震动（${vm.vibratorIds.size} 个振动器，幅度控制: ${vm.defaultVibrator.hasAmplitudeControl()}）"
                    } else {
                        "❌ 系统未暴露震动能力（该连接模式下震动通路不通）"
                    },
                    fontSize = 12.sp,
                    color = if (has) Color(0xFF81C784) else Color(0xFFE57373)
                )
                Row(
                    modifier = Modifier.horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text("通道:", fontSize = 11.sp, modifier = Modifier.padding(top = 12.dp), color = Color(0xFFAAAAAA))
                    if (patternChannel == "direct") {
                        Button(onClick = { patternChannel = "direct" }) { Text("直通", fontSize = 11.sp) }
                        OutlinedButton(onClick = { patternChannel = "framework" }) { Text("框架", fontSize = 11.sp) }
                    } else {
                        OutlinedButton(onClick = { patternChannel = "direct" }) { Text("直通", fontSize = 11.sp) }
                        Button(onClick = { patternChannel = "framework" }) { Text("框架", fontSize = 11.sp) }
                    }
                }
                Row(
                    modifier = Modifier.horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    val patterns = listOf(
                        "短震", "长震", "渐强", "渐弱", "脉冲", "机关枪",
                        "心跳", "波浪", "锯齿", "风暴", "极限10s"
                    )
                    patterns.forEachIndexed { idx, pn ->
                        if (idx == 0) {
                            Button(onClick = { playPattern(d, pn) }) { Text(pn) }
                        } else {
                            OutlinedButton(onClick = { playPattern(d, pn) }) { Text(pn) }
                        }
                    }
                    OutlinedButton(onClick = { stopPattern(d) }) { Text("停止") }
                }
            }
        }
    }

    @Composable
    private fun RumbleTimingCard() {
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(10.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text("震动时序测试", fontSize = 15.sp, style = MaterialTheme.typography.titleSmall)
                Text(
                    "流式=模拟游戏200Hz变振幅流(3秒)；单次=5秒长震。请求结束后，手柄真正停震的瞬间点红色按钮。",
                    fontSize = 11.sp, color = Color(0xFF888888)
                )
                devices.filter { it.vibratorManager.defaultVibrator.hasVibrator() }.forEach { d ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        Button(
                            onClick = { startTimingTest(d, "stream") },
                            enabled = timingStatus == "idle" || timingStatus == "done"
                        ) { Text("流式3秒") }
                        OutlinedButton(
                            onClick = { startTimingTest(d, "oneshot") },
                            enabled = timingStatus == "idle" || timingStatus == "done"
                        ) { Text("单次5秒") }
                        Text(d.name, fontSize = 11.sp, modifier = Modifier.padding(top = 12.dp))
                    }
                }
                when (timingStatus) {
                    "running" -> Text("震动中…", color = Color(0xFFE0B050), fontSize = 13.sp)
                    "waiting" -> Button(
                        onClick = { timingTapStop() },
                        modifier = Modifier.fillMaxWidth(),
                        colors = androidx.compose.material3.ButtonDefaults.buttonColors(
                            containerColor = Color(0xFFB3413A)
                        )
                    ) { Text("手柄停了就点我", fontSize = 16.sp) }
                }
                if (timingResult.isNotEmpty())
                    Text(timingResult, color = Color(0xFF81C784), fontSize = 14.sp)
            }
        }
    }

    @Composable
    private fun EventLogCard() {
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(10.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        "输入事件日志",
                        fontSize = 14.sp,
                        style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier
                            .weight(1f)
                            .padding(end = 4.dp),
                        maxLines = 1
                    )
                    OutlinedButton(
                        onClick = { eventLog.clear() },
                        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 4.dp)
                    ) { Text("清空") }
                }
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 300.dp)
                        .verticalScroll(rememberScrollState())
                ) {
                    eventLog.forEach {
                        Text(
                            it,
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace,
                            color = Color(0xFFCCCCCC)
                        )
                    }
                }
            }
        }
    }
}
