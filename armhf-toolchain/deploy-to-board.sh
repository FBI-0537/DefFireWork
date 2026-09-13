#!/usr/bin/env bash
#
# deploy-to-board.sh — 把交叉编译产物推到板子上并（可选）启动
#
# 为什么需要这个脚本:
#   项目是**在开发机上交叉编译、再把产物拷到板上跑**的 —— 板上性能不够,
#   不在板上编译。所以没有 "板上 git pull" 这条路, 一切都得从开发机推。
#   手敲 scp 有几个固定步骤容易漏 (尤其是首次部署时的 sysfs 权限)。
#
# 用法:
#     ./armhf-toolchain/deploy-to-board.sh fbi@192.168.1.100
#     ./armhf-toolchain/deploy-to-board.sh --run fbi@192.168.1.100   # 传完直接启动
#
# 必须显式给地址 (和 verify-on-board.sh 一致): 开发机上通常解析不了板子的
# 短主机名, 写死一个默认值只会让人误以为网络坏了。
#
# 传什么:
#     halloworld-gui / halloworld / TOOLCHAIN.txt  →  ~/
#     setup-board-permissions.sh                    →  ~/  (首次部署要跑一次)
#
# 注意: 脚本以目标用户 (fbi) 身份 ssh, 不用 root。root 只在**板上**执行
#       setup-board-permissions.sh 时才需要 (sudo)。

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build-armhf"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET=""
DO_RUN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --run) DO_RUN=1; shift ;;
        -h|--help)
            sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        -*)
            echo "未知参数: $1" >&2; exit 1 ;;
        *)
            TARGET="$1"; shift ;;
    esac
done

SSH_OPTS=(-o ConnectTimeout=8)

if [ -z "$TARGET" ]; then
    echo "用法: $0 [--run] <用户@板子IP>" >&2
    echo "例如: $0 fbi@192.168.1.100" >&2
    echo "      $0 --run root@192.168.1.100" >&2
    exit 1
fi

if ! printf '%s' "$TARGET" | grep -q '@'; then
    echo "目标要写成 用户@主机: $TARGET" >&2
    echo "板子的普通用户是 fbi, 例如: $0 fbi@192.168.1.100" >&2
    exit 1
fi

c_info() { printf '\033[36m%s\033[0m\n' "$*"; }
c_ok()   { printf '\033[32m%s\033[0m\n' "$*"; }
c_warn() { printf '\033[33m%s\033[0m\n' "$*"; }
c_err()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }

# ---------------------------------------------------------------------------
# 1) 本地产物
# ---------------------------------------------------------------------------
if [ ! -f "$BUILD_DIR/halloworld-gui" ]; then
    c_err "✗ 找不到 $BUILD_DIR/halloworld-gui"
    c_err "  先交叉编译:  ./build.sh gui-armhf"
    exit 1
fi

c_info "=== 待传输 ==="
for f in halloworld-gui halloworld TOOLCHAIN.txt; do
    if [ -f "$BUILD_DIR/$f" ]; then
        printf '    %-18s %s 字节\n' "$f" "$(stat -c%s "$BUILD_DIR/$f")"
    fi
done
printf '    %-18s (首次部署/权限修复用)\n' "setup-board-permissions.sh"

c_info "=== 目标: $TARGET ==="

# ---------------------------------------------------------------------------
# 2) 连通性
# ---------------------------------------------------------------------------
# 不吞 stderr: "Connection refused" / "Permission denied" 这些原文比我们自己的
# 提示更有用。密码登录也在这里完成 (ssh 从 /dev/tty 读密码, 不会卡住)。
if ! ssh "${SSH_OPTS[@]}" "$TARGET" true; then
    c_err ""
    c_err "✗ 连不上 $TARGET"
    c_err "  板子上电了吗 / IP 对不对 / 用户名是 fbi 吗"
    c_err "  提示: 板子的普通用户是 fbi, 例如 $0 fbi@192.168.1.100"
    exit 1
fi
c_ok "    ✓ SSH 可达"

# ---------------------------------------------------------------------------
# 3) 传输
# ---------------------------------------------------------------------------
# 注意这里是**从开发机推**。板上没有仓库, 也没有交叉编译器。
c_info "=== 传输 ==="
FILES=()
for f in halloworld-gui halloworld TOOLCHAIN.txt; do
    [ -f "$BUILD_DIR/$f" ] && FILES+=("$BUILD_DIR/$f")
done
FILES+=("$HERE/setup-board-permissions.sh")

scp -q "${SSH_OPTS[@]}" "${FILES[@]}" "$TARGET:~/"
c_ok "    ✓ 已传输到 ~/"

# ---------------------------------------------------------------------------
# 4) 权限检查
#
# 这是首次部署最容易卡住的地方: /sys/class/leds/*/brightness 默认 root:root,
# 普通用户只读, 于是界面点了没反应, 而 sudo 手敲却有效 —— 很难查。
# 这里以目标用户身份直接试写权限, 省得在界面上排查。
# ---------------------------------------------------------------------------
c_info "=== 检查 LED / 蜂鸣器写入权限 ==="
PERM_PROBE='for f in /sys/class/leds/beep/brightness /sys/class/leds/beep/trigger \
    /sys/class/leds/sys-led/brightness /sys/class/leds/sys-led/trigger; do
        if [ -w "$f" ]; then echo "OK   $f"; else echo "DENY $f"; fi
    done'

set +e
PERM_OUT="$(ssh "${SSH_OPTS[@]}" "$TARGET" "$PERM_PROBE" 2>/dev/null)"
set -e

if [ -z "$PERM_OUT" ]; then
    c_warn "    无法查询 (设备可能不存在, 或 ssh 执行失败)"
else
    printf '%s\n' "$PERM_OUT" | sed 's/^/    /'
fi

if printf '%s' "$PERM_OUT" | grep -q '^DENY'; then
    echo
    c_warn "⚠ 有设备文件当前用户不可写 —— 界面里点 LED / 蜂鸣器按钮不会有反应。"
    c_warn "  在板子上执行一次 (只需一次, 重启后仍生效):"
    echo
    echo "      sudo bash ~/setup-board-permissions.sh $(printf '%s' "$TARGET" | cut -d@ -f1)"
    echo "      sudo reboot"
    echo
    c_warn "  原因与验证见 WARNING.md 的「sysfs 权限」一节。"
else
    c_ok "    ✓ 权限正常"
fi

# ---------------------------------------------------------------------------
# 5) 启动 (可选)
# ---------------------------------------------------------------------------
if [ "$DO_RUN" -eq 1 ]; then
    c_info "=== 启动 GUI (DISPLAY=:0) ==="
    # 用 exec 让远程进程占住这个 ssh 会话; Ctrl+C 结束。
    # 板上有 LXDE/Openbox, 所以窗口会被 WM 接管 —— 全屏问题见 WARNING.md C-7。
    ssh -t "${SSH_OPTS[@]}" "$TARGET" 'cd ~ && DISPLAY=:0 ./halloworld-gui'
else
    echo
    c_info "启动:"
    echo "    ssh -t $TARGET 'cd ~ && DISPLAY=:0 ./halloworld-gui'"
    echo "    或者加 --run 让本脚本启动。"
fi
