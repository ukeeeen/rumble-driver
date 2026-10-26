#!/system/bin/sh
# 采样 rumble_driver 的 CPU 占用
P=$(pgrep -f "rumble_driver --uinput" | head -1)
if [ -z "$P" ]; then echo "未找到进程"; exit 1; fi
HZ=$(getconf CLK_TCK)
echo "PID=$P  HZ=$HZ"
echo "手柄: $(getevent -pl 2>/dev/null | grep -oE 'name: "(GameSir|Stadia)[^"]*"' | tr '\n' ' ')"
T0=$(awk '{print $14+$15}' /proc/$P/stat)
S0=$(date +%s)
sleep 60
T1=$(awk '{print $14+$15}' /proc/$P/stat)
S1=$(date +%s)
DT=$((S1-S0))
ET=$((T1-T0))
echo "采样 ${DT}s  CPU ticks=$ET"
awk -v t=$ET -v hz=$HZ -v dt=$DT 'BEGIN{printf "CPU 占用: %.3f%%\n", t/hz/dt*100}'
echo "--- 资源 ---"
grep -E "^(VmRSS|Threads)" /proc/$P/status
echo "--- 上下文切换 ---"
grep ctxt /proc/$P/status
