// gui.cpp — FireControlApp 主界面 (LVGL v9 + X11 后端)
//
// 目标平台: 正点原子 ATK-DLMP135 (STM32MP135) + Debian 12 + Xorg :0
//           屏幕 1024x600, 电容触摸屏
//
// 为什么用 LVGL 的 X11 后端, 而不是它更"嵌入式"的 framebuffer 后端:
//   板上跑着 Xorg, /dev/fb0 归 X server 管 —— 直接写 framebuffer 会和 Xorg 抢
//   同一块屏。X11 后端把绘制请求交给 X server, 部署方式一行都不用改; 触摸在
//   X11 里就是普通的鼠标事件, 键盘/滚轮也一并有驱动 (见 drivers/x11/)。
//   将来如果决定去掉 Xorg, 把这里的显示初始化换成 lv_linux_fbdev + lv_evdev
//   即可, 界面代码不用动。
//
// 中文怎么显示:
//   LVGL 开 FreeType, 运行时从板上的字体文件 (文泉驿正黑 wqy-zenhei.ttc) 取字形。
//   好处是改文案不用重新生成字库, 界面里出现任何汉字都能显示。板上本来就有
//   libfreetype (Xft 也在用), 没有引入新的运行时依赖。
//
// 本文件的历史与分工:
//   第一版由 Skywindfox 写: 裸 Xlib + Xft, 自己画矩形、自己算命中区域。
//   为什么用 Xft —— X11 核心位图字体只有 ISO-8859-1 编码, 画不出汉字, 中文会变
//   乱码; Xft 走 FreeType + fontconfig 才有完整 Unicode。
//   为什么不用 GTK/Qt —— 板子只有 437 MiB 内存, 它们空程序起步就是几十 MiB。
//   那一版的两个排查结论也都留在下面的实现里: 忽略窗口映射瞬间的杂散点击
//   (见 kIgnoreClicksMs), 以及字体必须确认真的含汉字字形。
//   当前这一版由 FBI-0537 重构到 LVGL —— 界面结构、字体加载、事件分发都换成
//   LVGL 那一套; "不用 GTK/Qt"这条理由对 LVGL 同样成立 (见 README 第 5 节)。
//
// 编译: 见 CMakeLists.txt, 目标 halloworld-gui
// 运行:
//     ./halloworld-gui                 全屏 —— 板上用这个 (按屏幕实际尺寸铺满)
//     ./halloworld-gui --windowed      1024x600 窗口 —— 开发机预览用
//     ./halloworld-gui --font=<文件>   指定字体文件 (默认按候选表自动找)

#include <lvgl.h>

#include <X11/Xlib.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "start.h"  // led_on / led_off (板载 sys-led)

namespace {

constexpr const char *kTitle = "FireControlApp";
constexpr const char *kText = "halloworld";

// 板上屏幕尺寸。仅用于窗口模式的默认值; 全屏时按实际屏幕尺寸。
constexpr int kBoardWidth = 1024;
constexpr int kBoardHeight = 600;

// 字号 (像素)
constexpr uint32_t kBigFontPx = 40;
constexpr uint32_t kSmallFontPx = 18;

// 启动后这段时间内的点击一律忽略。窗口刚映射时可能收到启动瞬间遗留的
// 杂散 ButtonPress, 不挡一下会"一启动就触发某个按钮"。
constexpr uint32_t kIgnoreClicksMs = 400;

// 主循环最长休眠时间, 防止 LVGL 给出的间隔异常时睡死。
constexpr uint32_t kMaxSleepMs = 10;

// 单位换算: 布局按相对比例算, 换屏幕尺寸不用改代码。
constexpr int pct(int total, int percent) { return total * percent / 100; }

// ---------------------------------------------------------------------------
// 字体
//
// 按"文件路径"找字体, 而不是像 Xft 那样按 fontconfig 字体名 —— 名字查找会
// 被 fontconfig 静默替换成别的字体, 拿到一个不含汉字的字体也察觉不到。
// 给一串候选路径, 取第一个能打开的。
//
// 板子上一共就这么几个可能的位置 (前两个是 Debian 官方包 fonts-wqy-zenhei 的
// 安装路径), 最后一个是 DejaVu —— 不含汉字, 只是保证程序还能跑起来。
// ---------------------------------------------------------------------------
const char *kFontCandidates[] = {
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/arphic/uming.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
};

bool fileReadable(const char *path)
{
    return access(path, R_OK) == 0;
}

const char *pickFontFile(const char *override_path)
{
    if (override_path != nullptr && *override_path != '\0') {
        if (fileReadable(override_path)) {
            return override_path;
        }
        std::fprintf(stderr, "警告: 指定的字体打不开, 改用自动查找: %s\n", override_path);
    }
    for (const char *path : kFontCandidates) {
        if (fileReadable(path)) {
            return path;
        }
    }
    std::fprintf(stderr,
                 "警告: 找不到中文字体, 界面上的汉字会显示成方框。\n"
                 "      板上装一个: sudo apt install fonts-wqy-zenhei\n"
                 "      或指定文件: ./halloworld-gui --font=/path/to/font.ttc\n");
    return nullptr;
}

lv_font_t *createFont(const char *file, uint32_t px, const char *what)
{
    lv_font_t *font = lv_freetype_font_create(file,
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              px, LV_FREETYPE_FONT_STYLE_NORMAL);
    if (font == nullptr) {
        std::fprintf(stderr, "警告: 加载字体失败 (%s): %s\n", what, file);
        return nullptr;
    }
    std::printf("  字体(%-8s): %s  [%u px]\n", what, file, px);
    return font;
}

// ---------------------------------------------------------------------------
// 按钮清单
//
// "有哪些按钮"是数据不是代码 —— 加按钮只改下面这张表, 布局按数量自动算。
// ---------------------------------------------------------------------------
struct ButtonSpec {
    const char *label;  // UTF-8 显示文字
    bool active;        // true = 已实现; false = 占位
};

constexpr int kNumPlaceholders = 4;
constexpr int kNumButtons = 1 + kNumPlaceholders;

// [0] 是右上角退出, 其余是底部按钮。
// active: 前两个接的是板载 sys-led 的开/关, 已经能用, 所以是 true;
//         后两个还没实现, 保持 false (按钮上会多画一圈 outline 标出"还没做")。
constexpr ButtonSpec kButtons[kNumButtons] = {
    {"退出", true},
    {"开灯", true}, {"关灯", true}, {"功能 3", false}, {"功能 4", false},
};

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------
lv_display_t *g_disp = nullptr;
lv_font_t *g_font_big = nullptr;
lv_font_t *g_font_small = nullptr;

uint32_t g_start_tick = 0;
bool g_quit = false;

bool inStartupGrace()
{
    const uint32_t elapsed = lv_tick_get() - g_start_tick;
    if (elapsed >= kIgnoreClicksMs) {
        return false;
    }
    std::printf("忽略启动瞬间的点击 (启动后 %u ms)\n", elapsed);
    std::fflush(stdout);
    return true;
}

// ---------------------------------------------------------------------------
// 事件回调
// ---------------------------------------------------------------------------
void onClickQuit(lv_event_t *e)
{
    LV_UNUSED(e);
    if (inStartupGrace()) {
        return;
    }
    std::printf("点击【退出】按钮, 退出\n");
    std::fflush(stdout);
    g_quit = true;
}

void onClickPlaceholder(lv_event_t *e)
{
    if (inStartupGrace()) {
        return;
    }
    const auto *label = static_cast<const char *>(lv_event_get_user_data(e));

    // 打印坐标便于板上核对触摸是否偏移 (板上待办之一)
    lv_point_t p{0, 0};
    lv_indev_t *indev = lv_indev_active();
    if (indev != nullptr) {
        lv_indev_get_point(indev, &p);
    }
    std::printf("点击【%s】坐标 (%d,%d) → ", label, static_cast<int>(p.x),
                static_cast<int>(p.y));

    // 底部前两个按钮: 板载 LED 开关。
    // 用标签判断是权宜之计 —— 等按钮多起来应该改成按索引派发 (ButtonSpec 里
    // 加一个动作枚举), 否则以后改文案会静默改掉功能。
    if (std::strcmp(label, kButtons[1].label) == 0) {
        std::printf("%s\n", led_on() == 0 ? "已开灯" : "开灯失败 (开发机无此设备)");
    } else if (std::strcmp(label, kButtons[2].label) == 0) {
        std::printf("%s\n", led_off() == 0 ? "已关灯" : "关灯失败 (开发机无此设备)");
    } else {
        // 移到这里的原打印: 还没绑动作的按钮仍然报坐标。
        std::printf("(占位, 功能待定)\n");
    }
    std::fflush(stdout);
}

void onKey(lv_event_t *e)
{
    const uint32_t key = lv_event_get_key(e);
    if (key == 'q' || key == 'Q' || key == LV_KEY_ESC) {
        std::printf("按键退出 (q / Esc)\n");
        std::fflush(stdout);
        g_quit = true;
    }
}

// 显示对象被删掉时会被调用 —— 无论是我们自己删, 还是窗口管理器点了关闭。
void onDisplayDeleted(lv_event_t *e)
{
    LV_UNUSED(e);
    g_disp = nullptr;  // 退出时别重复删
    g_quit = true;
}

// ---------------------------------------------------------------------------
// 界面
// ---------------------------------------------------------------------------
void applyButtonSkin(lv_obj_t *btn, bool active)
{
    // 白色底 + 1 像素黑边的扁平样式 (不跟随主题的圆角/阴影)
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_black(), 0);
    lv_obj_set_style_text_color(btn, lv_color_black(), 0);
    if (g_font_small != nullptr) {
        lv_obj_set_style_text_font(btn, g_font_small, 0);
    }

    // 触摸反馈: 按下时底色压深, 免得手指按下去没有任何回应
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xDDDDDD), LV_STATE_PRESSED);

    // 占位按钮再加一圈 outline, 画成双层边框, 视觉上区分"还没做"
    if (!active) {
        lv_obj_set_style_outline_width(btn, 1, 0);
        lv_obj_set_style_outline_pad(btn, 2, 0);
        lv_obj_set_style_outline_color(btn, lv_color_black(), 0);
    }
}

lv_obj_t *createButton(lv_obj_t *parent, const ButtonSpec &spec, lv_event_cb_t on_click)
{
    lv_obj_t *btn = lv_button_create(parent);
    applyButtonSkin(btn, spec.active);
    // 回调里要拿按钮文字, 直接借 user_data 传这个静态字符串
    lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED,
                        const_cast<char *>(spec.label));

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, spec.label);
    lv_obj_center(label);
    return btn;
}

void buildUi(int win_w, int win_h)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // --- 主标题: 屏幕正中 ---
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, kText);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    if (g_font_big != nullptr) {
        lv_obj_set_style_text_font(title, g_font_big, 0);
    }
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // --- 尺寸: 触摸屏上按钮不能太小, 手指触点约 40~50 px ---
    // 取屏高的 9%, 限制在 36..64 像素。
    int bh = pct(win_h, 9);
    if (bh < 36) {
        bh = 36;
    }
    if (bh > 64) {
        bh = 64;
    }
    const int margin = pct(win_w, 1);

    // --- 右上角退出按钮 ---
    lv_obj_t *quit_btn = createButton(scr, kButtons[0], onClickQuit);
    lv_obj_set_size(quit_btn, pct(win_w, 8), bh);
    lv_obj_align(quit_btn, LV_ALIGN_TOP_RIGHT, -margin, margin);

    // --- 底部占位按钮: 一行均分 ---
    // 用 flex 而不是自己算坐标: 每个按钮 flex_grow=1 自动等宽, 间距由
    // pad_column 给出 —— 换屏幕尺寸或改按钮数量都不用改布局代码。
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, margin, 0);
    lv_obj_set_size(row, win_w - 2 * margin, bh);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -margin);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 1; i < kNumButtons; i++) {
        lv_obj_t *btn = createButton(row, kButtons[i], onClickPlaceholder);
        lv_obj_set_height(btn, bh);
        lv_obj_set_flex_grow(btn, 1);
    }
}

// 开发机窗口模式下给鼠标画个指针。板上是触摸屏, 而且 LVGL 的 X11 后端
// 会把 X 光标隐藏掉, 所以只在窗口模式加。
void addMouseCursor(void)
{
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    while (indev != nullptr) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_obj_t *cur = lv_label_create(lv_layer_top());
            lv_label_set_text(cur, LV_SYMBOL_GPS);
            lv_indev_set_cursor(indev, cur);
            return;
        }
        indev = lv_indev_get_next(indev);
    }
}

} // namespace

int main(int argc, char **argv)
{
    bool windowed = false;
    const char *font_override = std::getenv("FIRECONTROL_FONT");

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--windowed") == 0) {
            windowed = true;
        } else if (std::strncmp(argv[i], "--font=", 7) == 0) {
            font_override = argv[i] + 7;
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::printf("用法: %s [--windowed] [--font=<字体文件>]\n"
                        "  (无参数)     全屏 —— 板上用这个 (按屏幕实际尺寸铺满)\n"
                        "  --windowed   1024x600 窗口模式 —— 开发机预览用\n"
                        "  --font=FILE  指定字体文件 (默认按候选表自动找)\n"
                        "  -h, --help   显示本帮助\n"
                        "\n"
                        "交互: 点右上角【退出】按钮退出, 或按 q / Esc\n",
                        argv[0]);
            return 0;
        }
    }

    // FreeType 按 UTF-8 处理文本, 同时保证 printf 出的中文不乱码
    if (std::setlocale(LC_ALL, "") == nullptr) {
        std::fprintf(stderr, "警告: setlocale 失败, 中文可能显示异常\n");
    }

    // -----------------------------------------------------------------------
    // 先探一下 X server
    //
    // 两件事: 全屏模式要知道屏幕实际尺寸 (LVGL 的 X11 后端只会按给它的尺寸
    // 建窗口, 不会自己查屏幕), 以及连不上时把排查方法讲清楚。
    // 这个连接只是查询, 查完就关 —— 真正绘图的连接由 LVGL 自己开。
    // -----------------------------------------------------------------------
    Display *probe = XOpenDisplay(nullptr);
    if (probe == nullptr) {
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

    const int scr_n = DefaultScreen(probe);
    const int screen_w = DisplayWidth(probe, scr_n);
    const int screen_h = DisplayHeight(probe, scr_n);
    const int screen_depth = DefaultDepth(probe, scr_n);
    const char *vendor = ServerVendor(probe);
    std::printf("已连接 X server\n");
    std::printf("  服务端 : %s\n", vendor);
    std::printf("  屏幕   : %dx%d, 深度 %d\n", screen_w, screen_h, screen_depth);
    XCloseDisplay(probe);

    // 窗口尺寸按屏幕实际尺寸算。板上是否有窗口管理器**尚未确认** (见
    // README「板上运行环境」)。有 WM 时它可能接管这个窗口; 没有时窗口就铺满屏幕。
    const int win_w = windowed ? kBoardWidth : screen_w;
    const int win_h = windowed ? kBoardHeight : screen_h;
    std::printf("  窗口   : %dx%d%s\n", win_w, win_h,
                windowed ? " (窗口模式)" : " (全屏)");

    // -----------------------------------------------------------------------
    // LVGL 初始化
    // -----------------------------------------------------------------------
    lv_init();

    // FreeType 要在 lv_init 之后初始化: 它把上下文挂在 LVGL 的全局对象上。
    if (lv_freetype_init(256) != LV_RESULT_OK) {
        std::fprintf(stderr, "警告: FreeType 初始化失败, 中文可能显示为方框\n");
    }

    const char *font_file = pickFontFile(font_override);
    if (font_file != nullptr) {
        g_font_big = createFont(font_file, kBigFontPx, "主标题");
        g_font_small = createFont(font_file, kSmallFontPx, "按钮标签");
    }

    g_disp = lv_x11_window_create(kTitle, win_w, win_h);
    if (g_disp == nullptr) {
        std::fprintf(stderr, "创建 X11 窗口失败\n");
        return 1;
    }
    lv_x11_inputs_create(g_disp, nullptr);
    lv_display_add_event_cb(g_disp, onDisplayDeleted, LV_EVENT_DELETE, nullptr);

    // -----------------------------------------------------------------------
    // q / Esc 要能全局生效
    //
    // LVGL 的键盘输入只发给"组里当前被聚焦的对象", 组里没有聚焦对象时按键会
    // 被直接丢掉。所以把屏幕本身加进默认组并聚焦它, 让屏幕当快捷键的接收者。
    // (按钮不进组, 所以点按钮不会把焦点抢走 —— 见 indev_click_focus。)
    // -----------------------------------------------------------------------
    lv_group_t *group = lv_group_get_default();
    if (group != nullptr) {
        lv_group_add_obj(group, lv_screen_active());
        lv_group_focus_obj(lv_screen_active());
    }
    lv_obj_add_event_cb(lv_screen_active(), onKey, LV_EVENT_KEY, nullptr);

    buildUi(win_w, win_h);

    if (windowed) {
        addMouseCursor();
    }

    std::printf("  布局   : 主标题居中, 退出按钮右上角, %d 个占位按钮在底部\n",
                kNumPlaceholders);
    std::printf("\n交互:\n");
    std::printf("  点右上角【退出】   退出程序\n");
    std::printf("  点底部占位按钮     打印占位提示 (功能待定)\n");
    std::printf("  按 q / Esc        退出程序\n");
    std::fflush(stdout);

    g_start_tick = lv_tick_get();

    while (!g_quit) {
        // X11 后端自己有两个定时器在读 X 事件 (显示 5 ms / 输入 1 ms),
        // 这里只需要按 LVGL 给的间隔循环调用即可。
        uint32_t idle = lv_timer_handler();
        if (idle == LV_NO_TIMER_READY || idle > kMaxSleepMs) {
            idle = kMaxSleepMs;
        }
        usleep(idle * 1000);
    }

    std::printf("退出\n");
    if (g_disp != nullptr) {
        lv_display_delete(g_disp);  // 关 X 窗口, 停掉 tick 线程
    }
    if (g_font_big != nullptr) {
        lv_freetype_font_delete(g_font_big);
    }
    if (g_font_small != nullptr) {
        lv_freetype_font_delete(g_font_small);
    }
    lv_deinit();
    return 0;
}
