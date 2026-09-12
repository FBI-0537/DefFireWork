// gui.cpp — FireControlApp 主窗口 (X11 + Xft)
//
// 目标平台: 正点原子 ATK-DLMP135 (STM32MP135) + Debian 12 + 精简 X server
//           屏幕 1024x600, 无窗口管理器, 电容触摸屏
//
// 为什么用 Xft 而不是 X11 核心字体:
//   X11 核心位图字体只有 ISO-8859-1 (Latin-1) 编码, 画不出汉字 —— 中文会变成
//   一串乱码字符。Xft 走 FreeType + fontconfig, 完整 Unicode 支持, 中文正常。
//   代价是运行时需要 libXft / libfontconfig / libfreetype 和一套中文字体
//   (板上用文泉驿正黑 wqy-zenhei)。
//
// 为什么不用 GTK/Qt:
//   依赖太重。板子总共 437 MiB 内存, GTK4 空程序就要 40-80 MiB。Xlib+Xft 只有
//   几 MiB。板上没有窗口管理器, 所以默认全屏, 自己 XMoveResizeWindow 占满屏幕。
//
// ⚠️ X11/Xft 是外部库, 交叉编译需要 ARM sysroot (见 README 第 4 节)。
//
// 编译 (注意: 注释行不能以反斜杠结尾, 会把下一行也吞进注释并触发 -Wcomment):
//     g++ -std=c++20 -Wall -Wextra -O2 gui.cpp -o halloworld-gui $(pkg-config --cflags --libs xft x11)
//
// 运行:
//     ./halloworld-gui              全屏 (板上用这个)
//     ./halloworld-gui --windowed   窗口模式 (开发机预览用)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

constexpr const char *kTitle = "FireControlApp";
constexpr const char *kText = "halloworld";

// 板上屏幕尺寸。仅用于窗口模式的默认值; 全屏时按实际屏幕尺寸。
constexpr int kBoardWidth = 1024;
constexpr int kBoardHeight = 600;

// 单位换算: 布局按相对比例算, 换屏幕尺寸不用改代码。
constexpr int pct(int total, int percent) { return total * percent / 100; }

// ---------------------------------------------------------------------------
// 按钮
//
// 裸 Xlib 没有控件概念, 按钮就是"画个矩形 + 画个标签 + 自己做命中检测"。
// 这个结构让"有哪些按钮"变成数据而不是代码 —— 加按钮只改下面的 kButtonLabels。
// ---------------------------------------------------------------------------
struct Button {
    const char *label;   // UTF-8 显示文字
    int x, y, w, h;      // 布局算出来的位置尺寸
    bool active;         // true = 已实现; false = 占位
};

constexpr int kNumPlaceholders = 4;
constexpr int kNumButtons = 1 + kNumPlaceholders;

// 按钮清单: [0] 是右上角退出, 其余是底部占位。
// 以后加/改按钮只动这个数组 —— 布局由 layoutButtons() 按数量自动算。
constexpr const char *kButtonLabels[kNumButtons] = {
    "退出",
    "功能 1", "功能 2", "功能 3", "功能 4",
};
constexpr bool kButtonActive[kNumButtons] = {
    true,
    false, false, false, false,
};

Button g_buttons[kNumButtons];

// 按窗口尺寸重新计算所有按钮的位置。窗口大小变化时也要重算。
void layoutButtons(int win_w, int win_h)
{
    for (int i = 0; i < kNumButtons; i++) {
        g_buttons[i].label = kButtonLabels[i];
        g_buttons[i].active = kButtonActive[i];
    }

    // --- 右上角退出按钮 ---
    // 触摸屏: 按钮不能太小, 否则手指点不准。取屏高的 9%, 限制在 36..64 像素。
    int bh = win_h * 9 / 100;
    if (bh < 36) bh = 36;
    if (bh > 64) bh = 64;

    const int margin = pct(win_w, 1);
    const int bw = pct(win_w, 8);

    g_buttons[0].w = bw;
    g_buttons[0].h = bh;
    g_buttons[0].x = win_w - bw - margin;
    g_buttons[0].y = margin;

    // --- 底部占位按钮: 横向均分 ---
    const int gap = pct(win_w, 1);
    const int total_gap = gap * (kNumPlaceholders + 1);
    const int pw = (win_w - total_gap) / kNumPlaceholders;
    const int py = win_h - bh - margin;

    for (int i = 0; i < kNumPlaceholders; i++) {
        g_buttons[1 + i].w = pw;
        g_buttons[1 + i].h = bh;
        g_buttons[1 + i].x = gap + i * (pw + gap);
        g_buttons[1 + i].y = py;
    }
}

bool hitTest(const Button &b, int x, int y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

// 单调时钟秒数。用于"映射后忽略窗口期"的竞态防护。
double now_sec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// ---------------------------------------------------------------------------
// Xft 字体
//
// 和核心字体的区别: 按"字体名"加载 (fontconfig 负责找文件), 支持任意 Unicode。
// 给一个候选列表, 取第一个能打开的 —— 不同系统装的中文字体不一样。
// ---------------------------------------------------------------------------
const char *kBigFontNames[] = {
    "WenQuanYi Zen Hei:size=40",      // 板上装的文泉驿正黑
    "Noto Sans CJK SC:size=40",
    "DejaVu Sans:size=40",
    "sans:size=40",
};
const char *kSmallFontNames[] = {
    "WenQuanYi Zen Hei:size=18",
    "Noto Sans CJK SC:size=18",
    "DejaVu Sans:size=18",
    "sans:size=18",
};

// UTF-8 -> Unicode 码点。返回写入的码点数, 0 表示空串。
//
// 需要自己解 UTF-8 的原因: XftDrawString32 收的是 FcChar32 数组, 而
// XftDrawStringUtf8 在部分 Xft 版本上处理多字节有兼容问题 —— 自己解最稳。
int utf8ToCodepoints(const char *s, FcChar32 *out, int max_out)
{
    int n = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    while (*p != 0 && n < max_out) {
        FcChar32 cp = 0;
        int extra = 0;
        if (*p < 0x80) {
            cp = *p;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F; extra = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F; extra = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07; extra = 3;
        } else {
            p++;              // 非法字节, 跳过
            continue;
        }
        p++;
        for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++, p++) {
            cp = (cp << 6) | (*p & 0x3F);
        }
        out[n++] = cp;
    }
    return n;
}

// 检查字体是否覆盖了文本里用到的所有字符。
//
// 为什么需要这个: XftFontOpenName 对**不存在的字体名不会返回 NULL** ——
// fontconfig 会做替换(substitution), 返回一个替补字体。如果替补字体没有汉字
// 字形, 中文就会画成方框(tofu), 而程序完全不知道自己拿到的是错字体。
// 所以必须自己验证覆盖率, 不合格就试下一个候选。
bool fontCoversText(Display *dpy, XftFont *font, const char *utf8)
{
    FcChar32 cps[256];
    const int n = utf8ToCodepoints(utf8, cps, 256);
    for (int i = 0; i < n; i++) {
        if (!XftCharExists(dpy, font, cps[i])) {
            return false;
        }
    }
    return true;
}

// 所有按钮文字里用到的字符 + 主标题, 作为"字体必须覆盖"的样例。
// 新增按钮文字时这里也要跟着加, 否则可能选到不含新字符的字体。
constexpr const char *kFontCoverageSample = "退出功能 1234halloworld";

XftFont *loadXftFont(Display *dpy, int scr, const char **names, int count, const char *what)
{
    for (int i = 0; i < count; i++) {
        XftFont *f = XftFontOpenName(dpy, scr, names[i]);
        if (f == nullptr) {
            continue;       // 真的打不开
        }
        if (fontCoversText(dpy, f, kFontCoverageSample)) {
            std::printf("  字体(%s): %s  [已校验字形覆盖]\n", what, names[i]);
            return f;
        }
        // 能打开但缺字形 —— 大概是被替换成了不含汉字的字体, 换下一个
        std::printf("  字体(%s): %s  [缺字形, 跳过]\n", what, names[i]);
        XftFontClose(dpy, f);
    }
    std::fprintf(stderr,
                 "警告: 找不到能显示全部字符的字体 (%s)。\n"
                 "      中文可能显示为方框。请安装中文字体, 例如:\n"
                 "        Fedora:  sudo dnf install google-noto-sans-cjk-fonts\n"
                 "        Debian:  sudo apt install fonts-wqy-zenhei\n", what);
    return nullptr;
}

// 测量 UTF-8 串的像素宽度
int textWidthUtf8(Display *dpy, XftFont *font, const char *s)
{
    FcChar32 cps[256];
    const int n = utf8ToCodepoints(s, cps, 256);
    if (n == 0) {
        return 0;
    }
    XGlyphInfo ext;
    XftTextExtents32(dpy, font, cps, n, &ext);
    return static_cast<int>(ext.xOff);
}

// 在指定基线上居中画一行 UTF-8 文字
void drawTextCentered(Display *dpy, XftDraw *draw, XftFont *font, XftColor *color,
                      int center_x, int baseline, const char *s)
{
    if (font == nullptr || s == nullptr) {
        return;
    }
    FcChar32 cps[256];
    const int n = utf8ToCodepoints(s, cps, 256);
    if (n == 0) {
        return;
    }
    const int w = textWidthUtf8(dpy, font, s);
    XftDrawString32(draw, color, font, center_x - w / 2, baseline, cps, n);
}

void drawButton(Display *dpy, Window win, GC gc, XftDraw *draw, XftFont *font,
                XftColor *color, const Button &b, int scr)
{
    XSetForeground(dpy, gc, BlackPixel(dpy, scr));

    // 已实现的按钮: 单层边框; 占位按钮: 双层边框, 视觉上区分"还没做"
    XDrawRectangle(dpy, win, gc,
                   static_cast<unsigned>(b.x), static_cast<unsigned>(b.y),
                   static_cast<unsigned>(b.w - 1), static_cast<unsigned>(b.h - 1));
    if (!b.active) {
        XDrawRectangle(dpy, win, gc,
                       static_cast<unsigned>(b.x + 2), static_cast<unsigned>(b.y + 2),
                       static_cast<unsigned>(b.w - 5), static_cast<unsigned>(b.h - 5));
    }

    // 标签垂直居中: 用字体的 ascent/descent 算基线
    const int baseline = b.y + (b.h + font->ascent - font->descent) / 2;
    drawTextCentered(dpy, draw, font, color, b.x + b.w / 2, baseline, b.label);
}

void redraw(Display *dpy, Window win, GC gc, XftDraw *draw,
            XftFont *big, XftFont *small, XftColor *color, int win_w, int win_h)
{
    const int scr = DefaultScreen(dpy);

    // 背景: 铺白。Xft 画字是叠加式的, 每次重绘必须先自己清背景。
    XSetForeground(dpy, gc, WhitePixel(dpy, scr));
    XFillRectangle(dpy, win, gc, 0, 0,
                   static_cast<unsigned>(win_w), static_cast<unsigned>(win_h));

    // 主标题: 屏幕正中
    if (big != nullptr) {
        const int baseline = (win_h + big->ascent - big->descent) / 2;
        drawTextCentered(dpy, draw, big, color, win_w / 2, baseline, kText);
    }

    // 按钮
    if (small != nullptr) {
        for (int i = 0; i < kNumButtons; i++) {
            drawButton(dpy, win, gc, draw, small, color, g_buttons[i], scr);
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    bool windowed = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--windowed") == 0) {
            windowed = true;
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::printf("用法: %s [--windowed]\n"
                        "  (无参数)     全屏 —— 板上用这个 (没有窗口管理器)\n"
                        "  --windowed   窗口模式 —— 开发机预览用\n"
                        "  -h, --help   显示本帮助\n"
                        "\n"
                        "交互: 点右上角【退出】按钮退出, 或按 q / Esc\n",
                        argv[0]);
            return 0;
        }
    }

    // Xft 内部要按当前 locale 处理编码, 设成 UTF-8。必须在打开字体前设。
    if (std::setlocale(LC_ALL, "") == nullptr) {
        std::fprintf(stderr, "警告: setlocale 失败, 中文可能显示异常\n");
    }

    Display *dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        std::fprintf(stderr,
                     "无法连接 X server。\n"
                     "  DISPLAY = %s\n"
                     "\n"
                     "板上排查:\n"
                     "  1) ls /tmp/.X11-unix/          确认 X server 在跑 (应看到 X0)\n"
                     "  2) export DISPLAY=:0           串口/ssh 会话里默认没有这个变量\n"
                     "  3) export XAUTHORITY=<xauth>   Xorg 带 -auth 时的认证文件\n",
                     std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "(未设置)");
        return 1;
    }

    const int scr = DefaultScreen(dpy);
    const int screen_w = DisplayWidth(dpy, scr);
    const int screen_h = DisplayHeight(dpy, scr);
    const Visual *visual = DefaultVisual(dpy, scr);
    const Colormap cmap = DefaultColormap(dpy, scr);

    // 实际窗口尺寸。窗口被缩放时由 ConfigureNotify 更新。
    int win_w = windowed ? kBoardWidth : screen_w;
    int win_h = windowed ? kBoardHeight : screen_h;

    Window win = XCreateSimpleWindow(
        dpy, RootWindow(dpy, scr),
        0, 0, static_cast<unsigned>(win_w), static_cast<unsigned>(win_h),
        0,                          // 无边框: 板上没有 WM, 边框没意义
        BlackPixel(dpy, scr),
        WhitePixel(dpy, scr));

    XStoreName(dpy, win, kTitle);

    XSelectInput(dpy, win,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                 StructureNotifyMask);

    // 有窗口管理器时(开发机上)点关闭按钮要能退出。
    // 注意: 不能声明成 const Atom —— XSetWMProtocols 的参数是非 const 的 Atom*,
    // gcc 容忍这种 const 不匹配, 但 clang 会直接报错。
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    if (!windowed) {
        XMoveResizeWindow(dpy, win, 0, 0,
                          static_cast<unsigned>(screen_w),
                          static_cast<unsigned>(screen_h));
    }

    XMapWindow(dpy, win);
    if (!windowed) {
        XRaiseWindow(dpy, win);
    }

    GC gc = XCreateGC(dpy, win, 0, nullptr);

    // Xft 绘图上下文 + 黑色
    XftDraw *draw = XftDrawCreate(dpy, win, const_cast<Visual *>(visual), cmap);
    XftColor black;
    const XRenderColor black_rc = {0, 0, 0, 0xffff};
    if (!XftColorAllocValue(dpy, const_cast<Visual *>(visual), cmap, &black_rc, &black)) {
        std::fprintf(stderr, "警告: 分配 Xft 颜色失败\n");
    }

    std::printf("已连接 X server\n");
    std::printf("  服务端 : %s\n", ServerVendor(dpy));
    std::printf("  屏幕   : %dx%d, 深度 %d\n",
                screen_w, screen_h, DefaultDepth(dpy, scr));
    std::printf("  窗口   : %dx%d%s\n", win_w, win_h,
                windowed ? " (窗口模式)" : " (全屏)");

    XftFont *big = loadXftFont(dpy, scr, kBigFontNames,
                               static_cast<int>(sizeof(kBigFontNames) / sizeof(kBigFontNames[0])),
                               "主标题");
    XftFont *small = loadXftFont(dpy, scr, kSmallFontNames,
                                 static_cast<int>(sizeof(kSmallFontNames) / sizeof(kSmallFontNames[0])),
                                 "按钮标签");

    layoutButtons(win_w, win_h);

    std::printf("  布局   : 主标题居中, 退出按钮右上角, %d 个占位按钮在底部\n",
                kNumPlaceholders);
    std::printf("\n交互:\n");
    std::printf("  点右上角【退出】   退出程序\n");
    std::printf("  点底部占位按钮     打印占位提示 (功能待定)\n");
    std::printf("  按 q / Esc        退出程序\n");
    std::fflush(stdout);

    // ---------------------------------------------------------------------
    // 竞态防护
    //
    // 窗口刚映射时可能收到启动瞬间遗留的杂散 ButtonPress。忽略映射后极短时间
    // 内的点击, 免得一启动就误触发某个按钮。
    //
    // 不用 X 事件时间戳: XExposeEvent 没有 time 字段, 拿不到首帧时刻。
    // ---------------------------------------------------------------------
    const double mapped_at = now_sec();
    constexpr double kIgnoreClicksSec = 0.4;

    bool done = false;
    while (!done) {
        XEvent ev;
        XNextEvent(dpy, &ev);

        switch (ev.type) {
        case Expose:
            if (ev.xexpose.count == 0) {
                redraw(dpy, win, gc, draw, big, small, &black, win_w, win_h);
            }
            break;

        case ConfigureNotify:
            win_w = ev.xconfigure.width;
            win_h = ev.xconfigure.height;
            layoutButtons(win_w, win_h);
            redraw(dpy, win, gc, draw, big, small, &black, win_w, win_h);
            break;

        case KeyPress: {
            const KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_q || ks == XK_Escape) {
                done = true;
            }
            break;
        }

        case ButtonPress: {
            const double t = now_sec();
            if (t - mapped_at < kIgnoreClicksSec) {
                std::printf("忽略启动瞬间的点击 (映射后 %.0f ms)\n",
                            (t - mapped_at) * 1000.0);
                std::fflush(stdout);
                break;
            }

            const int x = ev.xbutton.x;
            const int y = ev.xbutton.y;

            for (int i = 0; i < kNumButtons; i++) {
                if (!hitTest(g_buttons[i], x, y)) {
                    continue;
                }
                if (i == 0) {
                    std::printf("点击【%s】按钮, 退出\n", g_buttons[i].label);
                    std::fflush(stdout);
                    done = true;
                } else {
                    std::printf("点击【%s】(占位, 功能待定) 坐标 (%d,%d)\n",
                                g_buttons[i].label, x, y);
                    std::fflush(stdout);
                }
                break;
            }
            break;
        }

        case ClientMessage:
            if (static_cast<Atom>(ev.xclient.data.l[0]) == wm_delete) {
                done = true;
            }
            break;

        default:
            break;
        }
    }

    std::printf("退出\n");
    if (big != nullptr) XftFontClose(dpy, big);
    if (small != nullptr) XftFontClose(dpy, small);
    XftColorFree(dpy, const_cast<Visual *>(visual), cmap, &black);
    XftDrawDestroy(draw);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
