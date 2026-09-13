#!/usr/bin/env bash
#
# setup-board-permissions.sh — 让普通用户能控制板载 LED / 蜂鸣器
#
# 为什么需要这个:
#   /sys/class/leds/*/brightness 和 .../trigger 默认是 root:root 0644,
#   普通用户只读。所以 GUI 程序以 fbi 身份跑时, write_File 返回 -1,
#   现象是"点了按钮没反应", 而 sudo sh -c 'echo 1 > ...' 却有效。
#
# 做法: 内核 LED 框架本来就支持一个专门的 leds 组, 用 udev 规则把
#       /sys/class/leds/ 下的文件归到该组并放开组写权限。
#       这不是自创方案 —— 是 Debian/Ubuntu 上处理 LED 权限的标准做法。
#
# 用法 (在**板子上**运行):
#     sudo bash setup-board-permissions.sh              # 给当前用户
#     sudo bash setup-board-permissions.sh <用户名>      # 给指定用户
#
# 跑完之后**要重新登录** (组变更不会进已有会话), 或者执行 newgrp leds。

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

# root 本来就能写这些文件, 加进 leds 组没有意义。
if [ "$TARGET_USER" = "root" ]; then
    echo "root 不需要这个脚本 (本来就能写)" >&2
    exit 1
fi

echo "=== 1/4 创建 leds 组 ==="
groupadd -f leds
echo "    leds 组: $(getent group leds)"

echo "=== 2/4 写 udev 规则 ==="
RULE=/etc/udev/rules.d/90-leds.rules
cat > "$RULE" <<'EOF'
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
echo "    已写入 $RULE"
sed 's/^/    | /' "$RULE"

echo "=== 3/4 把 $TARGET_USER 加入 leds 组 ==="
if id -nG "$TARGET_USER" | tr ' ' '\n' | grep -qx leds; then
    echo "    已经在 leds 组里, 跳过"
else
    usermod -aG leds "$TARGET_USER"
    echo "    已加入"
fi

echo "=== 4/4 重新加载规则并触发 ==="
udevadm control --reload-rules
udevadm trigger --subsystem-match=leds
sleep 1

echo
echo "==================== 结果 ===================="
ls -l /sys/class/leds/beep/brightness /sys/class/leds/beep/trigger \
       /sys/class/leds/sys-led/brightness /sys/class/leds/sys-led/trigger 2>/dev/null

echo
echo "$TARGET_USER 的组: $(id -nG "$TARGET_USER")"
echo
echo "⚠ 组变更对**已有会话无效**。请二选一:"
echo "    - 注销重新登录 (推荐, 桌面环境需要这样)"
echo "    - 或在当前 shell 执行: newgrp leds"
echo
echo "验证 (重新登录后, 不要用 sudo):"
echo "    echo 1 > /sys/class/leds/beep/brightness   # 应该能响"
echo "    echo 0 > /sys/class/leds/beep/brightness   # 停"
