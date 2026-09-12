// gui.cpp — 显示 halloworld 的 X11 窗口
//
// 目标平台: 正点原子 ATK-DLMP135 (STM32MP135) + Debian 12 + 简 X server
//           屏幕 1024x600, 无窗口管理器, 电容触摸屏
//
// 为什么用裸 Xlib 而不是 GTK/Qt:
//   - 依赖只有 libX11。板子总共 437 MiB 内存, GTK4 空程序就要 40-80 MiB。
//   - 板上没有窗口管理器, 所以默认全屏, 自己用 XMoveResizeWindow 占满屏幕。
//   - 触摸屏在 X11 里就是绝对坐标指针, 单点触摸 → ButtonPress, 裸 Xlib 就能收。
//
// ⚠️ X11 程序无法交叉编译 (需要目标板的 ARM 版 libX11 + 头文件)。
//    必须在板子上编译: apt install gcc make pkg-config libx11-dev
//
// 编译 (注意: 注释行不能以反斜杠结尾, 会把下一行也吞进注释并触发 -Wcomment):
//     g++ -std=c++20 -Wall -Wextra -O2 gui.cpp -o halloworld-gui $(pkg-config --cflags --libs x11)
//
// 运行:
//     ./halloworld-gui              全屏 (板上用这个)
//     ./halloworld-gui --windowed   窗口模式 (开发机预览用)
//
// 退出: 按 q / Esc, 触摸或点击窗口, 或点窗口管理器的关闭按钮

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
namespace {

constexpr const char *kTitle = "FireControlApp";
constexpr const char *kText = "halloworld";

// 板上屏幕尺寸。全屏时用 XMoveResizeWindow 占满, 这里只是窗口模式的默认值。
constexpr int kBoardWidth = 1024;
constexpr int kBoardHeight = 600;

// 单调时钟秒数。用于判断"映射后多久"和双击间隔。
// 不用 X 事件时间戳: XExposeEvent 没有 time 字段, 拿不到首帧时刻。
double now_sec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// 画一行居中的文字。X11 核心字体是位图字体, 尺寸有限:
//   fixed   → 6x13   (几乎总是存在, 最小)
//   *-24-*  → 大号   (取决于服务端装了哪些字体)
// 大面积用大号字更好看, 但可能不存在, 所以先探测再回退。
void drawCenteredText(Display *dpy, Window win, GC gc, int win_w, int win_h)
{
    // 先试大号字, 失败再退回 fixed
    const char *fontNames[] = {
        "-*-helvetica-bold-r-normal--34-*-*-*-*-*-iso8859-1",
        "-*-fixed-bold-r-normal--34-*-*-*-*-*-iso8859-1",
        "-misc-fixed-bold-r-normal--34-*-*-*-*-*-iso8859-1",
        "fixed",
    };

    XFontStruct *font = nullptr;
    for (const char *name : fontNames) {
        font = XLoadQueryFont(dpy, name);
        if (font != nullptr) {
            break;
        }
    }
    if (font == nullptr) {
        std::fprintf(stderr, "警告: 找不到任何可用字体, 文字可能不显示\n");
        return;
    }

    XSetFont(dpy, gc, font->fid);
    XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));

    const int text_w = XTextWidth(font, kText, static_cast<int>(std::strlen(kText)));
    const int ascent = font->ascent;
    const int descent = font->descent;

    const int x = (win_w - text_w) / 2;
    const int y = (win_h + ascent - descent) / 2;

    XDrawString(dpy, win, gc, x, y, kText, static_cast<int>(std::strlen(kText)));

    XFreeFont(dpy, font);
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
                        "  -h, --help   显示本帮助\n", argv[0]);
            return 0;
        }
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

    // 实际窗口尺寸。窗口被缩放(或有 WM 干预)时由 ConfigureNotify 更新,
    // 重绘时用它而不是初始值 —— 否则文字不会重新居中。
    int win_w = windowed ? kBoardWidth : screen_w;
    int win_h = windowed ? kBoardHeight : screen_h;

    Window win = XCreateSimpleWindow(
        dpy, RootWindow(dpy, scr),
        0, 0, static_cast<unsigned>(win_w), static_cast<unsigned>(win_h),
        0,                          // 无边框: 板上没有 WM, 边框没意义
        BlackPixel(dpy, scr),       // 边框色
        WhitePixel(dpy, scr));      // 背景色(白底黑字)

    XStoreName(dpy, win, kTitle);

    // 订阅事件: 暴露要重绘, 键盘/鼠标/触摸要能退出
    XSelectInput(dpy, win,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                 StructureNotifyMask);

    // 有窗口管理器时(开发机上)点关闭按钮要能退出。
    // 注意: 不能声明成 const Atom —— XSetWMProtocols 的参数是非 const 的 Atom*,
    // gcc 容忍这种 const 不匹配, 但 clang 会直接报错。
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    // 全屏: 板上没有 WM, 位置尺寸得自己定, 否则窗口可能不在 (0,0) 或尺寸不对
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

    std::printf("已连接 X server\n");
    std::printf("  服务端 : %s\n", ServerVendor(dpy));
    std::printf("  屏幕   : %dx%d, 深度 %d\n",
                screen_w, screen_h, DefaultDepth(dpy, scr));
    std::printf("  窗口   : %dx%d%s\n", win_w, win_h,
                windowed ? " (窗口模式)" : " (全屏)");
    std::printf("  显示   : \"%s\"\n", kText);
    std::printf("\n退出: q / Esc / 双击窗口 / 点关闭按钮\n");
    std::fflush(stdout);

    // ---------------------------------------------------------------------
    // 竞态防护
    //
    // 窗口刚映射时可能收到启动瞬间遗留的杂散 ButtonPress, 如果"点一下退出"
    // 就会被它立刻关掉。实测 3 次里有 1 次中招(间歇性, 所以特别难查)。
    //
    // 不用 X 事件的时间戳过滤: XExposeEvent 根本没有 time 字段(只有
    // XButtonEvent/XKeyEvent 有), 拿不到"首帧时刻"。所以用单调时钟做
    // "映射后忽略窗口期", 不依赖 X 时间戳语义。
    // ---------------------------------------------------------------------
    const double mapped_at = now_sec();
    constexpr double kIgnoreClicksSec = 0.4;   // 映射后这段时间内的点击忽略
    constexpr double kDoubleClickSec = 0.6;    // 两次点击的间隔上限

    double first_click_at = 0.0;
    bool has_first_click = false;

    bool done = false;
    while (!done) {
        XEvent ev;
        XNextEvent(dpy, &ev);

        switch (ev.type) {
        case Expose:
            // 只在最后一个 Expose 时重绘, 避免重复劳动
            if (ev.xexpose.count == 0) {
                drawCenteredText(dpy, win, gc, win_w, win_h);
            }
            break;

        case ConfigureNotify:
            // 窗口尺寸变了就跟着更新, 下次重绘自动重新居中
            win_w = ev.xconfigure.width;
            win_h = ev.xconfigure.height;
            break;

        case KeyPress: {
            const KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_q || ks == XK_Escape) {
                done = true;
            }
            break;
        }

        case ButtonPress: {
            // 触摸屏的单点触摸到这里就是 ButtonPress。
            //
            // 不采用"点一下就退出": 映射瞬间的杂散事件会误触发。改成双击,
            // 并忽略映射后极短时间内的点击。
            const double now = now_sec();

            if (now - mapped_at < kIgnoreClicksSec) {
                std::printf("忽略启动瞬间的按下事件 (映射后 %.0f ms)\n",
                            (now - mapped_at) * 1000.0);
                std::fflush(stdout);
                break;
            }

            if (has_first_click && (now - first_click_at) <= kDoubleClickSec) {
                std::printf("双击确认, 退出\n");
                std::fflush(stdout);
                done = true;
            } else {
                first_click_at = now;
                has_first_click = true;
                std::printf("已点击一次, 再点一次退出 (点 (%d,%d))\n",
                            ev.xbutton.x, ev.xbutton.y);
                std::fflush(stdout);
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
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
