#!/usr/bin/env bash
#
# setup-board-permissions.sh — 让普通用户能控制板载 LED / 蜂鸣器, 并读写 GPIO
#
# 为什么需要这个:
#
#   (1) LED / 蜂鸣器 —— sysfs, 失败会被**静默吞掉**
#       /sys/class/leds/*/brightness 和 .../trigger 默认是 root:root 0644,
#       普通用户只读。所以 GUI 程序以 fbi 身份跑时, write_File 返回 -1,
#       现象是"点了按钮没反应", 而 sudo sh -c 'echo 1 > ...' 却有效。
#
#   (2) GPIO —— /dev 字符设备, 失败会明确报错
#       /dev/gpiochipN 由 devtmpfs 创建, 默认 root:root 0600(或 0660 root:root),
#       普通用户连 open 都不行, 所以 gpiodetect / gpioinfo 报
#       "unable to access GPIO chips: Permission denied", libgpiod 的
#       gpiod_chip_open_by_label() 同样失败。
#
#       这一条 Debian 自己不管: bookworm 的 libgpiod2 (1.6.3-1) 既不建 gpio 组
#       也不装 udev 规则, 对应的 Debian bug #1055231 已于 2025-07 以
#       "长期无响应" 关闭, 没有修复。所以只能自己上规则。
#       https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1055231
#
# 做法: (1) 内核 LED 框架本来就支持一个专门的 leds 组, 用 udev 规则把
#       /sys/class/leds/ 下的文件归到该组并放开组写权限。
#       这不是自创方案 —— 是 Debian/Ubuntu 上处理 LED 权限的标准做法。
#       (2) gpio 同理, 只是对象是 /dev/gpiochipN, 子系统是 gpio 而不是 leds,
#       两条规则各管各的, 不能互相覆盖。
#
# 用法 (在**板子上**运行):
#     sudo bash setup-board-permissions.sh              # 给当前用户
#     sudo bash setup-board-permissions.sh <用户名>      # 给指定用户
#
# 跑完之后**要重新登录** (组变更不会进已有会话), 或者执行 newgrp leds。
# 建议直接 sudo reboot —— 见文末说明。

set -euo pipefail

TARGET_USER="${1:-${SUDO_USER:-}}"

if [ "$(id -u)" -ne 0 ]; then
    echo "需要 root: sudo bash $0 [用户名]" >&2
    exit 1
fi

if [ -z "$TARGET_USER" ]; then
    echo "无法确定目标用户。用法: sudo bash $0 <用户名>" >&2
    exit 1
fi

if ! id "$TARGET_USER" >/dev/null 2>&1; then
    echo "用户不存在: $TARGET_USER" >&2
    exit 1
fi

# root 本来就能读写这些设备, 加进组没有意义。
if [ "$TARGET_USER" = "root" ]; then
    echo "root 不需要这个脚本 (本来就能读写)" >&2
    exit 1
fi

echo "=== 1/6 创建 leds / gpio 组 ==="
groupadd -f leds
groupadd -f gpio
echo "    leds 组: $(getent group leds)"
echo "    gpio 组: $(getent group gpio)"

echo "=== 2/6 写 LED 的 udev 规则 ==="
RULE_LED=/etc/udev/rules.d/90-leds.rules
cat > "$RULE_LED" <<'EOF'
# 让 leds 组能读写 /sys/class/leds/ 下的属性文件 (brightness / trigger 等)。
#
# 内核 LED 框架注册设备时会重建这些文件的权限, 所以必须在 udev 阶段设,
# 用 chmod 手动改会在下次设备注册时被打回原样 (重启也失效)。
#
# 规则顺序: 60-*.rules (内核自带) 先跑, 90-* 在之后, 所以这里能覆盖默认权限。
# chmod g=u 表示"组权限 = 属主权限": 属主能写, 组就能写, 不用写死 0660。
#
# 注意: udev 不做 shell 变量展开, 用户名不能写进规则里 —— 所以用组, 不用 ACL。
SUBSYSTEM=="leds", ACTION=="add", RUN+="/bin/chgrp -R leds /sys%p", RUN+="/bin/chmod -R g=u /sys%p"
EOF
echo "    已写入 $RULE_LED"
sed 's/^/    | /' "$RULE_LED"

echo "=== 3/6 写 GPIO 的 udev 规则 ==="
RULE_GPIO=/etc/udev/rules.d/90-gpio.rules
cat > "$RULE_GPIO" <<'EOF'
# 让 gpio 组能读写 /dev/gpiochipN。
#
# 和 90-leds.rules 是两条独立规则: gpiochip 是字符设备, 属于 gpio 子系统,
# 与 leds 子系统没有任何关系, 不能指望 leds 那条覆盖到它。
#
# KERNEL=="gpiochip*" 把范围限死在 /dev/gpiochipN, 不会误伤老式 sysfs GPIO
# 接口 (/sys/class/gpio/export 等)。
#
# 如果本内核里 gpiochip 的子系统名不是 "gpio", 这条规则不会命中 —— 用
#     udevadm info -q property /dev/gpiochip0 | grep SUBSYSTEM
# 确认后再改。脚本最后会自动核对组有没有真的改过来。
SUBSYSTEM=="gpio", KERNEL=="gpiochip*", ACTION=="add", RUN+="/bin/chgrp gpio /dev/%k", RUN+="/bin/chmod g=u /dev/%k"
EOF
echo "    已写入 $RULE_GPIO"
sed 's/^/    | /' "$RULE_GPIO"

echo "=== 4/6 把 $TARGET_USER 加入 leds / gpio 组 ==="
for g in leds gpio; do
    if id -nG "$TARGET_USER" | tr ' ' '\n' | grep -qx "$g"; then
        echo "    已经在 $g 组里, 跳过"
    else
        usermod -aG "$g" "$TARGET_USER"
        echo "    已加入 $g"
    fi
done

echo "=== 5/6 重新加载规则并触发 ==="
udevadm control --reload-rules
udevadm trigger --subsystem-match=leds
udevadm trigger --subsystem-match=gpio
sleep 1

echo
echo "==================== 结果 ===================="
# 原来这里是裸 ls: 少一个文件就非零退出, 在 set -e 下直接把脚本打断,
# 而"文件不存在"恰恰是用户最需要看到结果的时刻。所以改成只报告。
echo "--- LED (组应是 leds) ---"
ls -l /sys/class/leds/beep/brightness /sys/class/leds/beep/trigger \
       /sys/class/leds/sys-led/brightness /sys/class/leds/sys-led/trigger 2>/dev/null || \
    echo "    ⚠ 上面有文件没列出来 —— 板子的 LED 名字可能不是 beep / sys-led," \
         "用 ls /sys/class/leds/ 看一下实际名字。"

# gpiochip 是字符设备, 触发后组必须变成 gpio。没变就说明规则没生效 ——
# 这是本次新增的部分, 最容易"看起来跑完了其实没用", 所以显式核对。
echo "--- GPIO (组应是 gpio) ---"
GPIO_DEVS=(/dev/gpiochip*)
if [ -e "${GPIO_DEVS[0]}" ]; then
    ls -l /dev/gpiochip* 2>/dev/null || true
    for d in "${GPIO_DEVS[@]}"; do
        grp="$(stat -c %G "$d")"
        if [ "$grp" != "gpio" ]; then
            echo "    ⚠ $d 的组仍是 $grp, 规则没生效。查子系统名:"
            echo "        udevadm info -q property $d | grep SUBSYSTEM"
        fi
    done
else
    echo "    ⚠ 没找到 /dev/gpiochip* —— 本内核没暴露 GPIO 字符设备?"
fi

echo
echo "$TARGET_USER 的组: $(id -nG "$TARGET_USER")"
echo
echo "⚠ 组变更对**已有会话无效**。请二选一:"
echo "    - 注销重新登录 / sudo reboot (推荐)"
echo "    - 或在当前 shell 执行: newgrp leds   (gpio 组同理)"
echo
echo "验证 (重新登录后, 不要用 sudo):"
echo "    echo 1 > /sys/class/leds/beep/brightness   # 应该能响"
echo "    echo 0 > /sys/class/leds/beep/brightness   # 停"
echo "    gpiodetect                                 # 应该能列出 gpiochipN"
