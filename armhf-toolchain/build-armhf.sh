#!/usr/bin/env bash
# build-armhf.sh — 用容器交叉编译 FireControlApp 到 armhf (Linux/macOS)
#
# 用法:
#   ./armhf-toolchain/build-armhf.sh              构建 GUI + 控制台
#   ./armhf-toolchain/build-armhf.sh --rebuild    先重建环境镜像再构建
#   ./armhf-toolchain/build-armhf.sh --shell      进容器交互 (调试工具链用)
#
# 环境变量:
#   IMAGE=...        环境镜像名 (默认 firecontrol-armhf:bookworm)
#   BASE_IMAGE=...   基础镜像, 网络访问不了 docker.io 时用镜像站:
#                    BASE_IMAGE=docker.m.daocloud.io/library/debian:bookworm-slim \
#                        ./armhf-toolchain/build-armhf.sh
#
# 需要 docker 或 podman(自动选择)。Fedora 上装 podman 即可, 不需要 docker。
# Windows 用同目录的 build-armhf.ps1。
#
# 构建方式: 把源码 COPY 进镜像层编译, 产物再用 cp 取回宿主 —— 全程不依赖 bind mount。
#           原因见下方"在容器里配置 + 构建"一节的注释。
#
# 产物: build-armhf/ (含 TOOLCHAIN.txt 工具链指纹)

set -euo pipefail

IMAGE="${IMAGE:-firecontrol-armhf:bookworm}"
# 基础镜像可用镜像站覆盖（例如网络访问不了 docker.io 时）:
#     BASE_IMAGE=docker.m.daocloud.io/library/debian:bookworm-slim ./build-armhf.sh
BASE_IMAGE="${BASE_IMAGE:-}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-armhf"
DOCKERFILE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

c_info() { printf '\033[36m%s\033[0m\n' "$*"; }
c_ok()   { printf '\033[32m%s\033[0m\n' "$*"; }
c_err()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }

# 从 Dockerfile 解析基础镜像名。
#
# 只认 `ARG BASE=xxx` 这种带默认值的写法 —— Dockerfile 用的是
#     ARG BASE=debian:bookworm-slim
#     FROM ${BASE}
# 而 `FROM ${BASE}` 里没有镜像名, 只有一个引用。若 Dockerfile 写成
# `FROM debian:bookworm-slim` 这种字面量, 这里也一并支持。
dockerfile_base_default() {
    local dockerfile="$DOCKERFILE_DIR/Dockerfile"
    local from_line arg_line

    from_line="$(grep -iE '^[[:space:]]*FROM[[:space:]]' "$dockerfile" | head -1 || true)"
    # 去掉末尾可能存在的 "AS stage" 别名, 取镜像名
    from_line="$(printf '%s' "$from_line" | awk '{print $2}')"

    case "$from_line" in
        *'${'*|*'$'*)
            # FROM 是变量引用, 真正的名字在 `ARG BASE=` 的默认值里
            arg_line="$(grep -iE '^[[:space:]]*ARG[[:space:]]+BASE=' "$dockerfile" | head -1 || true)"
            printf '%s' "$arg_line" | sed -E 's/^[[:space:]]*ARG[[:space:]]+BASE=//I' | awk '{print $1}'
            ;;
        *)
            printf '%s' "$from_line"
            ;;
    esac
}

# ---------------------------------------------------------------------------
# 挂载宿主源码目录所需的参数
#
# Fedora 默认 SELinux Enforcing, 容器默认读不到挂进来的宿主目录(报 Permission denied)。
# 两种解法:
#     :Z             给宿主目录打 container_file_t 标签 —— 会**永久改写宿主目录的
#                    SELinux 标签**, 而且 restorecon 因为同时写了 fcontext 定制记录
#                    会拒绝恢复(实测踩到过)。对源码仓库是侵入性的, 不采用。
#     label=disable  只对本容器关闭 SELinux 隔离, **不动宿主标签**。采用这个。
#
# 代价: 容器内不再受 SELinux 约束。但构建进程以当前用户身份运行(非 root),
#       只读源码 + 写构建目录, 风险可接受。要更强的隔离可自行改用 :Z, 并清楚
#       它会改宿主标签。
# ---------------------------------------------------------------------------
MOUNT_OPTS=()
if command -v getenforce >/dev/null 2>&1 && [ "$(getenforce)" = "Enforcing" ]; then
    MOUNT_OPTS=(--security-opt label=disable)
fi

# ---------------------------------------------------------------------------
# 选容器运行时: 优先 docker, 退回 podman
# ---------------------------------------------------------------------------
if command -v docker >/dev/null 2>&1; then
    RUNTIME=docker
elif command -v podman >/dev/null 2>&1; then
    RUNTIME=podman
else
    c_err "✗ 找不到 docker 或 podman"
    c_err "    Fedora:  sudo dnf install podman"
    c_err "    其它:    https://docs.docker.com/get-docker/"
    exit 1
fi

# ---------------------------------------------------------------------------
# 镜像管理
#
# 镜像跑在开发机(amd64)上做交叉编译, 不是运行环境, 所以不需要 qemu/binfmt。
# ---------------------------------------------------------------------------
image_exists() {
    "$RUNTIME" image inspect "$IMAGE" >/dev/null 2>&1
}

case "${1:-}" in
    --rebuild)
        c_info "=== 重建镜像 $IMAGE ==="
        "$RUNTIME" build -t "$IMAGE" "$DOCKERFILE_DIR"
        ;;
    --shell)
        image_exists || { c_info "镜像不存在, 先构建"; "$RUNTIME" build -t "$IMAGE" "$DOCKERFILE_DIR"; }
        c_info "=== 进入容器 ($IMAGE) ==="
        exec "$RUNTIME" run --rm -it \
            "${MOUNT_OPTS[@]}" \
            --user "$(id -u):$(id -g)" \
            -v "$PROJECT_ROOT:/work" \
            -w /work \
            "$IMAGE" /bin/bash
        ;;
    "")
        ;;
    *)
        c_err "未知参数: $1"; exit 1
        ;;
esac

if ! image_exists; then
    c_info "=== 镜像 $IMAGE 不存在, 开始构建 ==="
    c_info "    (首次较慢; 之后除非 --rebuild 否则复用)"

    # -----------------------------------------------------------------------
    # 基础镜像: 取 Dockerfile 里的默认值, 可用 BASE_IMAGE 覆盖(镜像站)
    #
    # 不能直接取 `FROM` 行的镜像名 —— Dockerfile 里写的是
    #       ARG BASE=debian:bookworm-slim
    #       FROM ${BASE}
    # `FROM` 的最后一个字段是字面量 "${BASE}" (一个 build-arg 引用), 拿去 pull
    # 会去拉一个叫 "${BASE}" 的镜像, 必然失败, 而且报错信息看起来像网络问题。
    # 所以优先取 `ARG BASE=` 的默认值, 那才是真正的镜像名。
    # -----------------------------------------------------------------------
    if [ -z "$BASE_IMAGE" ]; then
        BASE_IMAGE="$(dockerfile_base_default)"
    fi
    if [ -z "$BASE_IMAGE" ]; then
        c_err "✗ 无法从 Dockerfile 解析基础镜像名, 请用 BASE_IMAGE=... 显式指定"
        exit 1
    fi

    BUILD_ARGS=()
    BUILD_ARGS+=(--build-arg "BASE=$BASE_IMAGE")

    # -----------------------------------------------------------------------
    # 基础镜像可达性预检
    #
    # 不加这一步时, 如果 registry 不可达(网络受限 / 公司防火墙 / 守护进程没起来),
    # `build` 会在拉基础镜像时长时间挂住 —— 看起来像卡死, 实际在等 TCP 超时。
    # 这里先快速探一下, 给出明确原因而不是让你干等。
    #
    # 本地已有该基础镜像时跳过预检(离线构建场景)。
    # -----------------------------------------------------------------------
    if [ -n "$BASE_IMAGE" ] && ! "$RUNTIME" image inspect "$BASE_IMAGE" >/dev/null 2>&1; then
        c_info "    预检基础镜像可达性: $BASE_IMAGE"
        if ! timeout 60 "$RUNTIME" pull --quiet "$BASE_IMAGE" >/dev/null 2>&1; then
            c_err ""
            c_err "✗ 拉不到基础镜像: $BASE_IMAGE"
            c_err ""
            c_err "  常见原因:"
            c_err "    - 网络受限, 访问不了镜像仓库 (docker.io / quay.io 等)"
            c_err "    - 守护进程没有在运行"
            c_err ""
            c_err "  处理办法(按可行性排序):"
            c_err ""
            c_err "    1) 用镜像站覆盖基础镜像:"
            c_err "       BASE_IMAGE=docker.m.daocloud.io/library/debian:bookworm-slim \\"
            c_err "           ./armhf-toolchain/build-armhf.sh"
            c_err ""
            c_err "    2) 或在能联网的机器上拉好再拷过来:"
            c_err "       docker pull debian:bookworm-slim"
            c_err "       docker save debian:bookworm-slim | gzip > debian.tar.gz"
            c_err "       # 目标机: gunzip -c debian.tar.gz | docker load"
            c_err ""
            c_err "    3) podman 可在 /etc/containers/registries.conf 配镜像加速"
            exit 1
        fi
        c_info "    ✓ 基础镜像可达"
    fi

    c_info "    基础镜像: $BASE_IMAGE"
    "$RUNTIME" build "${BUILD_ARGS[@]}" -t "$IMAGE" "$DOCKERFILE_DIR"
fi

mkdir -p "$BUILD_DIR"

# ---------------------------------------------------------------------------
# 在容器里配置 + 构建
#
# 用 Dockerfile.build-armhf + COPY, 而不是 `-v` 挂载源码目录。
#
# 为什么不挂载: 在部分环境(实测 podman + 受限沙箱)挂进来的目录**只读**, 报
#     mkdir: cannot create directory '/work/build-armhf/CMakeFiles': Permission denied
# 与 SELinux/属主无关 —— 干净的 user_home_t 目录同样写不了。COPY 不依赖挂载,
# 任何环境都能构建。代价是没有增量编译, 每次全量。
#
# 产物用 `cp` 从构建出来的镜像里取回宿主, 也不依赖挂载。
# ---------------------------------------------------------------------------
c_info "=== 容器内交叉编译 ($RUNTIME) ==="
c_info "    方式: COPY 源码进镜像层 (不依赖 bind mount)"

BUILD_IMAGE="firecontrol-armhf-build:tmp"
BUILD_LOG="$(mktemp)"
trap 'rm -f "$BUILD_LOG"' EXIT

if ! "$RUNTIME" build \
        --build-arg "BASE_IMAGE=$IMAGE" \
        -f "$DOCKERFILE_DIR/Dockerfile.build-armhf" \
        -t "$BUILD_IMAGE" \
        "$PROJECT_ROOT" 2>&1 | tee "$BUILD_LOG" | grep -E '^\s*\[|Class:|Machine:|Flags:|error|Error|FAIL' ; then
    c_err "✗ 容器内构建失败, 完整日志见: $BUILD_LOG"
    tail -30 "$BUILD_LOG" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 从构建镜像取回产物
#
# podman/docker 都没有"从镜像直接拷文件"的命令, 标准做法是先 create 一个容器
# 再 cp。create 不启动容器, 所以很快。
# ---------------------------------------------------------------------------
c_info "=== 取回产物 ==="
mkdir -p "$BUILD_DIR"
CID="$("$RUNTIME" create "$BUILD_IMAGE")"
for f in halloworld-gui halloworld; do
    if "$RUNTIME" cp "$CID:/work/build-armhf/$f" "$BUILD_DIR/$f" 2>/dev/null; then
        printf '  ✓ %s\n' "$f"
    fi
done
"$RUNTIME" rm "$CID" >/dev/null 2>&1 || true
"$RUNTIME" rmi "$BUILD_IMAGE" >/dev/null 2>&1 || true

# ---------------------------------------------------------------------------
# 产物隔离: 写工具链指纹
#
# 为什么需要: 同一个 halloworld-gui 可能来自 Docker(glibc/gcc) 或 Zig 或板上原生
# 编译。三者编译器与 libc 都不同, 出问题时第一步就是确认"这个二进制是谁编的"。
# 指纹文件随产物一起走, 板上也能查。
# ---------------------------------------------------------------------------
c_info "=== 写工具链指纹 ==="
FINGERPRINT="$("$RUNTIME" run --rm "$IMAGE" cat /etc/firecontrol-toolchain.txt)"
{
    echo "# 本目录产物由以下工具链生成 —— 出问题时先看这里确认来源"
    echo "$FINGERPRINT"
    echo "build_mode   = copy (容器内编译, 不依赖 bind mount)"
    echo "host_runtime = $RUNTIME"
    echo "host_arch    = $(uname -m)"
    echo "built_local  = $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$BUILD_DIR/TOOLCHAIN.txt"
sed 's/^/    /' "$BUILD_DIR/TOOLCHAIN.txt"

# ---------------------------------------------------------------------------
# 结果
# ---------------------------------------------------------------------------
echo
c_info "=== 产物 ==="
FOUND=0
for exe in halloworld-gui halloworld; do
    f="$BUILD_DIR/$exe"
    if [ -f "$f" ]; then
        printf '  %-20s %9s 字节  %s\n' "$exe" "$(stat -c %s "$f")" \
            "$(LC_ALL=C file -b "$f" | cut -d, -f1-3)"
        FOUND=1
    fi
done

if [ "$FOUND" -eq 0 ]; then
    c_err "✗ build-armhf/ 里没有可执行文件, 检查上面的构建输出"
    exit 1
fi

if [ -x "$HOME/.local/bin/arm-linux-gnueabihf-readelf" ]; then
    echo
    echo "  动态依赖:"
    LC_ALL=C "$HOME/.local/bin/arm-linux-gnueabihf-readelf" -d "$BUILD_DIR/halloworld-gui" 2>/dev/null \
        | grep NEEDED | sed 's/^/    /' || true
fi

echo
c_ok "✓ 完成: $BUILD_DIR"
cat <<'NOTE'

  部署到板子:
      scp build-armhf/halloworld-gui root@<板子IP>:~/
      scp build-armhf/TOOLCHAIN.txt  root@<板子IP>:~/
      ssh root@<板子IP> 'DISPLAY=:0 ./halloworld-gui'

  板上自检(确认产物与板子匹配):
      ./armhf-toolchain/verify-on-board.sh root@<板子IP>
NOTE
