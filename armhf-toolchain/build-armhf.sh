#!/usr/bin/env bash
# build-armhf.sh — 用 Docker 交叉编译 FireControlApp 到 armhf (Linux/macOS)
#
# 用法:
#   ./armhf-toolchain/build-armhf.sh              构建 GUI + 控制台 + 逻辑层
#   ./armhf-toolchain/build-armhf.sh --rebuild    先重建镜像再构建
#   ./armhf-toolchain/build-armhf.sh --shell      进容器交互 (调试工具链用)
#
# Windows 用同目录的 build-armhf.ps1。
#
# 产物: build-armhf/ (含 TOOLCHAIN.txt 指纹, 见下)

set -euo pipefail

IMAGE="${IMAGE:-firecontrol-armhf:bookworm}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-armhf"
DOCKERFILE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

c_info() { printf '\033[36m%s\033[0m\n' "$*"; }
c_ok()   { printf '\033[32m%s\033[0m\n' "$*"; }
c_err()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }

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
    # 基础镜像可达性预检
    #
    # 不加这一步时, 如果 registry 不可达(网络受限 / 公司防火墙 / 守护进程没起来),
    # `docker build` 会在拉基础镜像时长时间挂住 —— 看起来像卡死, 实际在等 TCP 超时。
    # 这里先快速探一下, 给出明确原因而不是让你干等。
    # -----------------------------------------------------------------------
    BASE_IMAGE="$(grep -iE '^FROM[[:space:]]' "$DOCKERFILE_DIR/Dockerfile" | head -1 | awk '{print $2}')"
    if [ -n "$BASE_IMAGE" ] && ! "$RUNTIME" image inspect "$BASE_IMAGE" >/dev/null 2>&1; then
        c_info "    预检基础镜像可达性: $BASE_IMAGE"
        if ! timeout 25 "$RUNTIME" pull --quiet "$BASE_IMAGE" >/dev/null 2>&1; then
            c_err ""
            c_err "✗ 拉不到基础镜像: $BASE_IMAGE"
            c_err ""
            c_err "  常见原因:"
            c_err "    - 网络受限, 访问不了镜像仓库 (docker.io / quay.io 等)"
            c_err "    - Docker Desktop / 守护进程没有在运行"
            c_err ""
            c_err "  处理办法:"
            c_err "    - 换可达的仓库: 改 Dockerfile 的 FROM 行"
            c_err "    - 或在能上网的机器上拉好再拷过来:  docker save / docker load"
            c_err "    - podman 可在 /etc/containers/registries.conf 配镜像加速"
            c_err ""
            c_err "  注意: 原来的 Zig + sysroot 路径仍然可用 (见 cmake/toolchain-armhf.cmake),"
            c_err "        但它只编纯逻辑层与控制台程序, 不编 GUI。"
            exit 1
        fi
        c_info "    ✓ 基础镜像可达"
    fi

    "$RUNTIME" build -t "$IMAGE" "$DOCKERFILE_DIR"
fi

mkdir -p "$BUILD_DIR"

# ---------------------------------------------------------------------------
# 在容器里配置 + 构建
#
# --user $(id -u):$(id -g) 很关键: 否则容器以 root 写文件, 产物的属主变成 root,
# 在宿主上没法修改也没法删。
# ---------------------------------------------------------------------------
c_info "=== 容器内交叉编译 ($RUNTIME / $IMAGE) ==="

"$RUNTIME" run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$PROJECT_ROOT:/work" \
    -w /work \
    "$IMAGE" \
    /bin/bash -c '
        set -e
        cmake -B build-armhf -S /work \
              -DCMAKE_TOOLCHAIN_FILE=/work/armhf-toolchain/toolchain-docker-armhf.cmake \
              -DCMAKE_BUILD_TYPE=Release \
              -DBUILD_TESTING=OFF \
              -DWITH_GUI=ON
        cmake --build build-armhf -j "$(nproc)"
    '

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
