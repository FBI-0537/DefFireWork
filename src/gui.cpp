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
// 编译: 见 CMakeLists.txt, 目标 deffire-gui-dev
// 运行:
//     ./deffire-gui-dev                 全屏 —— 板上用这个 (按屏幕实际尺寸铺满)
//     ./deffire-gui-dev --windowed      1024x600 窗口 —— 开发机预览用
//     ./deffire-gui-dev --font=<文件>   指定字体文件 (默认按候选表自动找)
//
// 界面结构 (分页):
//     首页            : 只放"进入调试页"的入口, 保持干净 (6 个硬件按钮不在这里)
//     LED 调试页      : LED 开 / LED 关 / LED 心跳 / 返回
//     蜂鸣器调试页    : 蜂鸣器开 / 蜂鸣器关 / 蜂鸣器心跳 / 返回
//   顶栏 (品牌 / 状态灯 / 退出) 三页完全一致; 切页**只重建底栏按钮** (见 g_page_dirty)。
//   q / Esc 在每一页都是"退出程序", 不随页面变化 —— 板子上是触摸屏, 键盘只是开发机用。

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
constexpr uint32_t kBigFontPx = 40;     // 主标题
constexpr uint32_t kSmallFontPx = 18;   // 标题栏文字 / 按钮标签
constexpr uint32_t kStatusFontPx = 16;  // 状态说明这类"次要小字"

// ---------------------------------------------------------------------------
// 配色: 深色工业简约风
//
// 全部集中在这里 —— 控件代码里不出现颜色字面量, 想换风格只改这一段。
// 取值是按"触摸屏 + 机房/车间环境"挑的:
//   底压到近黑、文字提到近白 => 强光下也看得清, 夜间不刺眼;
//   强调色(安全琥珀)只出现在少数几个地方: 标题左侧竖条、主标题下划线、
//   状态灯之外的按下态描边 —— 目的是"一眼能找到当前可操作的位置"。
// ---------------------------------------------------------------------------
constexpr uint32_t kColBg         = 0x121417;  // 屏幕底
constexpr uint32_t kColPanel      = 0x1A1D21;  // 顶部标题栏 / 底部操作栏
constexpr uint32_t kColSurface    = 0x23272E;  // 按钮表面
constexpr uint32_t kColSeparator  = 0x2E343B;  // 描边 / 分隔线
constexpr uint32_t kColDividerDim = 0x24282E;  // 未实现按钮的描边(压暗)
constexpr uint32_t kColPressed    = 0x30363E;  // 按下态底色
constexpr uint32_t kColText       = 0xE6E8EA;  // 主文字
constexpr uint32_t kColTextDim    = 0x8A9199;  // 次要文字 / 未实现按钮
constexpr uint32_t kColAccent     = 0xFFB020;  // 强调色 (安全琥珀)
constexpr uint32_t kColOk         = 0x35C46A;  // 状态灯: 就绪/运行中

// 启动后这段时间内的点击一律忽略。窗口刚映射时可能收到启动瞬间遗留的
// 杂散 ButtonPress, 不挡一下会"一启动就触发某个按钮"。
constexpr uint32_t kIgnoreClicksMs = 400;

// 主循环最长休眠时间, 防止 LVGL 给出的间隔异常时睡死。
constexpr uint32_t kMaxSleepMs = 10;

// 指针输入采样周期 (ms)。LVGL 默认把它建成 LV_DEF_REFR_PERIOD —— 但"画面多久
// 更新一次"和"多久看一眼有没有按下"是两件事。X11 后端的输入处理只保存最新状态
// (lv_x11_input.c: 按下置 true、松开置 false), 所以一次按下+松开如果落在同一个
// 采样窗口里, 这次点击**整次消失**。
// 实测 (2026-09-15, XTest 注入计时): 默认周期下按住 10ms 的快点击丢 60%;
// 采样压到 5ms 后同条件 0 丢失。详见 lv_conf.h 的 LV_DEF_REFR_PERIOD 注释。
constexpr uint32_t kIndevReadMs = 5;

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
                 "      或指定文件: ./deffire-gui-dev --font=/path/to/font.ttc\n");
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

// 按钮绑定的动作。**不要用标签文字来判断点了哪个按钮** —— 那样改一次文案就
// 会静默改掉功能, 而且编译器不会报错。加动作时在这里加一项, 然后在
// onAction() 的 switch 里补一个 case (漏了会有 -Wswitch 警告, 因为没写 default)。
enum class Action {
    // Nothing **必须留在第一位**: ButtonSpec 是聚合类型, 表里漏写 action 字段时,
    // action 会被值初始化为 0 (= 第一个枚举值)。第一位是 Nothing, 漏写就退化成
    // "占位", 顶多点了没反应; 若第一位是 LedOn, 漏写就会静默点亮 LED。
    Nothing,
    LedOn,
    LedOff,
    LedHeartbeat,
    BuzzerOn,
    BuzzerOff,
    BuzzerHeartbeat,
    OutBuzzerOn,   // 外部无源蜂鸣器 (GPIOA:6): 只置标志, 脉冲由主循环发
    OutBuzzerOff,
    LedSingleSettingPage,        // 首页 → LED 调试页
    BuzzerSingleSettingPage,     // 首页 → 蜂鸣器调试页
    OutBuzzerSingleSettingPage,  // 首页 → 外部无源蜂鸣器调试页
    BackToHome,                  // 调试页 → 首页
};

struct ButtonSpec {
    const char *label;  // UTF-8 显示文字
    bool active;        // true = 已实现; false = 占位 (按钮上多画一圈 outline)
    Action action;      // 点击后执行的动作; 只有 active=true 时才有意义
};

// 顶栏右侧的【退出】。它有自己的回调 (onClickQuit), action 不参与派发, 所以填
// Nothing —— 但字段一样要写全 (见上面 Nothing 的说明)。
constexpr ButtonSpec kQuitButton = {"退出", true, Action::Nothing};

// 一个页面 = 一组底栏按钮 + 页名 + **本页每行放几个**。
//
// "每行几个"是**每页各自**的参数, 不是全局常量: 首页排 3 列(好放成 2 行 × 3 列),
// 调试页排 4 列(一行放完最宽裕)。加页面 / 改列数只动下面这张表, 布局自己算。
//
// 一行里每个按钮等宽(flex_grow), 所以在 1024 宽的屏上: 4 列 ≈ 243 px、3 列 ≈ 328 px
// —— 中文标签都不会被挤到换行; 触摸目标建议 ≥ 48 px 见方, 高度由 btn_h 保证。
struct PageSpec {
    const char *name;  // 页名: 打印到控制台, 也显示在内容区
    const ButtonSpec *buttons;
    int count;
    int columns;  // 本页底栏每行放几个按钮
};

// 首页: 只留"进调试页"的入口 —— 硬件按钮都挪进各自的调试页, 首页保持干净。
//
// 后面那几个 active=false 的是**占位**: 按钮画成暗描边、点了只打印"(占位, 功能待定)",
// 用来把 2 行 × 3 列排满。有真实功能时把 active 改 true 并绑一个 Action,
// **别忘了在 onAction() 的 switch 里补 case**(漏了 -Wswitch 会报, 这就是那道防线)。
// 【外部无源蜂鸣器】就是顶掉原来"功能 8"那一格 —— 格数不变, 首页仍是 2 行 × 3 列。
constexpr ButtonSpec kHomeButtons[] = {
    {"LED 调试", true, Action::LedSingleSettingPage},
    {"蜂鸣器 调试", true, Action::BuzzerSingleSettingPage},
    {"外部无源蜂鸣器", true, Action::OutBuzzerSingleSettingPage},
    {"功能 A", false, Action::Nothing},
    {"功能 B", false, Action::Nothing},
    {"功能 C", false, Action::Nothing},
};

// LED 调试页: 单独操作 LED 的开关与心跳, 末位是返回
constexpr ButtonSpec kLedButtons[] = {
    {"LED 开", true, Action::LedOn},
    {"LED 关", true, Action::LedOff},
    {"LED 心跳", true, Action::LedHeartbeat},
    {"返回", true, Action::BackToHome},
};

// 蜂鸣器调试页: 同上, 换成蜂鸣器
constexpr ButtonSpec kBuzzerButtons[] = {
    {"蜂鸣器开", true, Action::BuzzerOn},
    {"蜂鸣器关", true, Action::BuzzerOff},
    {"蜂鸣器心跳", true, Action::BuzzerHeartbeat},
    {"返回", true, Action::BackToHome},
};

// 外部无源蜂鸣器调试页: 接在 GPIOA:6 上的无源蜂鸣器。
//
// 和上面那一页的区别: 走 libgpiod 手动写电平, **不归内核 leds-gpio 管**,
// 所以它没有 trigger 文件 —— 这页里**没有"心跳"按钮**, 板载那个的"心跳"是
// 内核 LED 框架的功能, 外部这个没有对应的东西 (开/关就是全部动作)。
constexpr ButtonSpec kOutBuzzerButtons[] = {
    {"外部蜂鸣器 开", true, Action::OutBuzzerOn},
    {"外部蜂鸣器 关", true, Action::OutBuzzerOff},
    {"返回", true, Action::BackToHome},
};

// 数组长度在编译期取出来 —— 原来 kNumButtons 是手写的数字, 往表里加一项忘了改
// 就变成越界初始化 (这次就是这么坏的)。
template <typename T, int N>
constexpr int countOf(const T (&)[N])
{
    return N;
}

// 当前显示哪一页。顺序必须与 kPages 一致, 见下面的 static_assert。
enum class Page { Home, Led, Buzzer, OutBuzzer };

constexpr PageSpec kPages[] = {
    {"首页", kHomeButtons, countOf(kHomeButtons), 3},  // 2 行 × 3 列
    {"LED 调试页", kLedButtons, countOf(kLedButtons), 4},
    {"蜂鸣器调试页", kBuzzerButtons, countOf(kBuzzerButtons), 4},
    {"外部无源蜂鸣器调试页", kOutBuzzerButtons, countOf(kOutBuzzerButtons), 3},
};

static_assert(static_cast<int>(Page::Home) == 0 && static_cast<int>(Page::Led) == 1 &&
                  static_cast<int>(Page::Buzzer) == 2 &&
                  static_cast<int>(Page::OutBuzzer) == 3,
              "Page 的顺序必须与 kPages 一致");

// columns 会当除数用, 0 会让 rowsOf() 直接除零 —— 在编译期挡住, 别等运行期崩
constexpr bool columnsAllValid()
{
    for (const PageSpec &p : kPages) {
        if (p.columns < 1) {
            return false;
        }
    }
    return true;
}

static_assert(columnsAllValid(), "kPages 里每页的 columns 必须 >= 1");

// 某页要排几行; 以及所有页里最多的行数。
// 底栏高度按"最多的那页"算, 切页时底栏高度不变, 内容区不会上下跳。
constexpr int rowsOf(int count, int columns)
{
    return (count + columns - 1) / columns;
}

constexpr int maxRows()
{
    int m = 1;
    for (const PageSpec &p : kPages) {
        const int r = rowsOf(p.count, p.columns);
        if (r > m) {
            m = r;
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------
lv_display_t *g_disp = nullptr;
lv_font_t *g_font_big = nullptr;
lv_font_t *g_font_small = nullptr;
lv_font_t *g_font_status = nullptr;

lv_obj_t *g_footer_bar = nullptr;  // 底栏容器: 只建一次, 切页时清空重建里面的按钮
lv_obj_t *g_page_label = nullptr;  // 内容区那行状态说明 (页名 + 上一步结果)

Page g_page = Page::Home;

// 切页请求。事件回调里**只置这个标志**, 真正的重建放到主循环里做 ——
// 否则会在"正在派发某个按钮的点击"的过程中删掉它所在的底栏。
bool g_page_dirty = false;

// 上一步动作的结果, 显示在内容区 (nullptr = 刚切过页, 只显示页名)
const char *g_last_outcome = nullptr;

// 布局尺寸在 buildUi() 里算好存下来 —— 切页重建底栏时还要用 (见 buildPageButtons)
int g_win_w = kBoardWidth;
int g_btn_h = 48;
int g_margin = 10;

uint32_t g_start_tick = 0;
bool g_quit = false;

// 内容区那行小字: "页名", 或 "页名 · 上一步结果"
void refreshPageLabel()
{
    if (g_page_label == nullptr) {
        return;
    }
    const PageSpec &page = kPages[static_cast<int>(g_page)];
    if (g_last_outcome != nullptr) {
        lv_label_set_text_fmt(g_page_label, "%s · %s", page.name, g_last_outcome);
    } else {
        lv_label_set_text(g_page_label, page.name);
    }
}

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

void onAction(lv_event_t *e)
{
    if (inStartupGrace()) {
        return;
    }
    const auto *spec = static_cast<const ButtonSpec *>(lv_event_get_user_data(e));

    // 打印坐标便于板上核对触摸是否偏移 (板上待办之一)
    lv_point_t p{0, 0};
    lv_indev_t *indev = lv_indev_active();
    if (indev != nullptr) {
        lv_indev_get_point(indev, &p);
    }
    const int px = static_cast<int>(p.x);
    const int py = static_cast<int>(p.y);

    // 结果先算出来再统一打印 + 显示到内容区 —— 控制台的格式保持不变
    // (板上核对触摸/硬件的记录都依赖 "点击【x】坐标 (x,y) → 结果" 这一行)。
    //
    // 初值给个兜底串而不是 nullptr: 下面 switch 覆盖了所有枚举值, 但枚举的底层
    // 整数理论上可以是表外的值, GCC 因此无法证明它非空 (-Wformat-overflow)。
    // 用兜底串既消掉警告, 也真的兜住了那种情况 —— 而不是靠加 default 把
    // -Wswitch 那道"漏了 case"的防线拆掉。
    const char *outcome = "(未知动作)";
    bool page_switch = false;

    // 按动作派发。switch 里不写 default —— 以后往 Action 里加成员但忘了加
    // case 时, -Wswitch 会在编译期报出来, 而不是让那个按钮默默什么都不做。
    switch (spec->action) {
    case Action::LedOn:
        outcome = led_onboard_on() == 0 ? "LED 已点亮" : "失败 (开发机无此设备)";
        break;
    case Action::LedOff:
        outcome = led_onboard_off() == 0 ? "LED 已熄灭" : "失败 (开发机无此设备)";
        break;
    case Action::LedHeartbeat:
        outcome = led_onboard_set_heartbeat() == 0 ? "LED 心跳已开启"
                                                   : "失败 (开发机无此设备)";
        break;
    case Action::BuzzerOn:
        outcome = buzzer_onboard_on() == 0 ? "蜂鸣器已响" : "失败 (开发机无此设备)";
        break;
    case Action::BuzzerOff:
        outcome = buzzer_onboard_off() == 0 ? "蜂鸣器已停" : "失败 (开发机无此设备)";
        break;
    case Action::BuzzerHeartbeat:
        outcome = buzzer_onboard_set_heartbeat() == 0 ? "蜂鸣器心跳已开启"
                                                      : "失败 (开发机无此设备)";
        break;
    // 外部无源蜂鸣器: **只置标志**, 真正的脉冲在主循环里发 (buzzer_set_beep())。
    // 不能在这里"响一下"就算完 —— 按钮回调返回之后蜂鸣器不会自己保持,
    // 一次 1 µs 的脉冲听不见东西。
    case Action::OutBuzzerOn:
        out_buzzer_status = true;
        outcome = "外部无源蜂鸣器已开";
        break;
    case Action::OutBuzzerOff:
        out_buzzer_status = false;
        outcome = "外部无源蜂鸣器已停";
        break;
    case Action::Nothing:
        outcome = "(占位, 功能待定)";
        break;

    // 切页: 只改 g_page 并置标志, 底栏由主循环重建 (见 g_page_dirty 的说明)
    case Action::LedSingleSettingPage:
        g_page = Page::Led;
        g_page_dirty = true;
        page_switch = true;
        outcome = "进入 LED 调试页";
        break;
    case Action::BuzzerSingleSettingPage:
        g_page = Page::Buzzer;
        g_page_dirty = true;
        page_switch = true;
        outcome = "进入蜂鸣器调试页";
        break;
    case Action::OutBuzzerSingleSettingPage:
        g_page = Page::OutBuzzer;
        g_page_dirty = true;
        page_switch = true;
        outcome = "进入外部无源蜂鸣器调试页";
        break;
    case Action::BackToHome:
        g_page = Page::Home;
        g_page_dirty = true;
        page_switch = true;
        outcome = "返回首页";
        break;
    }

    std::printf("点击【%s】坐标 (%d,%d) → %s\n", spec->label, px, py, outcome);

    // 切页时页名自己会变, 就不把"进入 xx 页"当成结果留在屏幕上; 设备动作则
    // 把结果显示出来 —— 板上不用连控制台也能看到刚才那一下成没成。
    if (page_switch) {
        g_last_outcome = nullptr;
    } else {
        g_last_outcome = outcome;
        refreshPageLabel();
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
//
// 三段式工业面板: 顶部标题栏 (品牌 + 状态灯 + 退出) / 中间内容区 / 底部操作栏。
// 颜色一律取自上表的调色板, 控件代码里不出现颜色字面量;
// 布局用 flex, 换屏幕尺寸或改按钮数量都不用改坐标。
// ---------------------------------------------------------------------------

// 纯布局用的"素容器": 透明、无描边、不滚动、无内边距
lv_obj_t *createPane(lv_obj_t *parent)
{
    lv_obj_t *pane = lv_obj_create(parent);
    lv_obj_remove_flag(pane, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(pane, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pane, 0, 0);
    lv_obj_set_style_radius(pane, 0, 0);
    lv_obj_set_style_pad_all(pane, 0, 0);
    lv_obj_set_style_shadow_width(pane, 0, 0);  // 主题给容器带了卡片样式, 阴影一律去掉
    return pane;
}

// 顶栏/底栏的公共外观: 深色底 + 靠内侧一条 1px 分隔线
lv_obj_t *createBar(lv_obj_t *parent, int w, int h, lv_border_side_t side)
{
    lv_obj_t *bar = createPane(parent);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, side, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(kColSeparator), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return bar;
}

void applyButtonSkin(lv_obj_t *btn, bool active)
{
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

    // 已实现的按钮: 有实体面 + 常规描边; 未实现的压暗成"底", 一眼能看出还没做
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? kColSurface : kColBg), 0);
    lv_obj_set_style_border_color(btn,
                                  lv_color_hex(active ? kColSeparator : kColDividerDim), 0);

    // 触摸反馈: 底色抬一档 + 描边点亮成强调色 —— 手指按下去要立刻有回应
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColPressed), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(kColAccent), LV_STATE_PRESSED);
}

lv_obj_t *createButton(lv_obj_t *parent, const ButtonSpec &spec, lv_event_cb_t on_click)
{
    lv_obj_t *btn = lv_button_create(parent);
    applyButtonSkin(btn, spec.active);
    // 回调里要拿整个 spec (标签 + 动作), 借 user_data 把表里那一项的地址传过去。
    // kButtons 是 constexpr 静态存储, 生命周期覆盖整个程序, 指针一直有效。
    lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED,
                        const_cast<ButtonSpec *>(&spec));

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, spec.label);
    // 颜色显式设在标签上: 主题对 label 类有自己的一套配色, 靠父对象继承不保险
    lv_obj_set_style_text_color(label,
                                lv_color_hex(spec.active ? kColText : kColTextDim), 0);
    if (g_font_small != nullptr) {
        lv_obj_set_style_text_font(label, g_font_small, 0);
    }
    lv_obj_center(label);
    return btn;
}

// 顶部标题栏: 左 = 品牌(强调色竖条 + 名字), 中 = 状态灯, 右 = 退出
void buildHeader(lv_obj_t *scr, int win_w, int bar_h, int margin)
{
    lv_obj_t *bar = createBar(scr, win_w, bar_h, LV_BORDER_SIDE_BOTTOM);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_hor(bar, margin, 0);

    // --- 左: 品牌 ---
    lv_obj_t *brand = createPane(bar);
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brand, 10, 0);

    lv_obj_t *tick = createPane(brand);
    lv_obj_set_size(tick, 3, 18);
    lv_obj_set_style_bg_color(tick, lv_color_hex(kColAccent), 0);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(brand);
    lv_label_set_text(title, kTitle);
    lv_obj_set_style_text_color(title, lv_color_hex(kColText), 0);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    if (g_font_small != nullptr) {
        lv_obj_set_style_text_font(title, g_font_small, 0);
    }

    // --- 中: 状态灯 ---
    // 现在只表示"程序在跑"。以后接上传感器/联动状态时, 改这里的文字和灯色即可。
    lv_obj_t *state = createPane(bar);
    lv_obj_set_size(state, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(state, 8, 0);

    lv_obj_t *led = createPane(state);
    lv_obj_set_size(led, 10, 10);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(led, lv_color_hex(kColOk), 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(led, 8, 0);  // 一点辉光, 让灯真的"亮"起来
    lv_obj_set_style_shadow_color(led, lv_color_hex(kColOk), 0);
    lv_obj_set_style_shadow_opa(led, LV_OPA_50, 0);

    lv_obj_t *state_text = lv_label_create(state);
    lv_label_set_text(state_text, "运行中");
    lv_obj_set_style_text_color(state_text, lv_color_hex(kColTextDim), 0);
    if (g_font_status != nullptr) {
        lv_obj_set_style_text_font(state_text, g_font_status, 0);
    }

    // --- 右: 退出 ---
    lv_obj_t *quit_btn = createButton(bar, kQuitButton, onClickQuit);
    lv_obj_set_size(quit_btn, pct(win_w, 9), bar_h - 26);
}

// 中间内容区: 主标题 + 一条强调色短线 + 一行小字 (页名 / 上一步结果)
void buildContent(lv_obj_t *scr, int win_w, int top, int h)
{
    lv_obj_t *content = createPane(scr);
    lv_obj_set_size(content, win_w, h);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 14, 0);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, kText);
    lv_obj_set_style_text_color(title, lv_color_hex(kColText), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    if (g_font_big != nullptr) {
        lv_obj_set_style_text_font(title, g_font_big, 0);
    }

    // 装饰用的强调色短线: 不承载信息, 只是把视线钉在标题上
    lv_obj_t *rule = createPane(content);
    lv_obj_set_size(rule, 72, 3);
    lv_obj_set_style_bg_color(rule, lv_color_hex(kColAccent), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    // 页名 / 上一步结果。切页和每次点按钮之后由 refreshPageLabel() 更新 ——
    // 板上没接控制台时, 这一行就是"刚才那一下成没成"的唯一反馈。
    lv_obj_t *status = lv_label_create(content);
    lv_obj_set_style_text_color(status, lv_color_hex(kColTextDim), 0);
    if (g_font_status != nullptr) {
        lv_obj_set_style_text_font(status, g_font_status, 0);
    }
    lv_label_set_text(status, kPages[static_cast<int>(g_page)].name);
    g_page_label = status;
}

// 底部操作栏的容器: 深色面板 + 顶边分隔线。
//
// 容器只建一次, 里面的按钮由 buildPageButtons() 按当前页重建 —— 切页就是
// "清空底栏, 按新页的按钮表重排"。高度按 maxRows() (所有页里最多的行数) 算,
// 切页时底栏高度不变, 内容区不会上下跳。
//
// 外层竖直 flex、每行横向 flex 均分 —— 加按钮只改页表, 布局自动重排,
// "每行几个"取自本页的 PageSpec::columns。返回底栏高度, 供 buildUi 算内容区。
int buildFooter(lv_obj_t *scr, int win_w, int btn_h, int margin)
{
    const int row_gap = btn_h / 3;  // 行间距, 随按钮高度缩放
    const int bar_h = maxRows() * btn_h + (maxRows() - 1) * row_gap + 2 * margin;

    lv_obj_t *bar = createBar(scr, win_w, bar_h, LV_BORDER_SIDE_TOP);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(bar, margin, 0);
    lv_obj_set_style_pad_row(bar, row_gap, 0);
    // createBar 默认是横向 flex, 这里要竖直排行
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
    // 主轴(main place)用 CENTER, 不是 SPACE_BETWEEN: 首页 2 行时正好排满(无差别),
    // 而只有 1 行的调试页会在空出来的高度里**垂直居中** —— 按钮不会全挤在分隔线
    // 下面、底下拖一条空带。代价: 切到调试页时那一行比首页第 1 行低约半行。
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    g_footer_bar = bar;
    return bar_h;
}

// 按当前页 (g_page) 重建底栏按钮: 初始化和每次切页都走这里。
// 尺寸不用重算 —— 底栏高度已由 buildFooter 按 maxRows() 固定;
// 本页每行放几个由 PageSpec::columns 决定。
void buildPageButtons()
{
    if (g_footer_bar == nullptr) {
        return;
    }
    lv_obj_clean(g_footer_bar);  // 删掉上一页的按钮

    const PageSpec &page = kPages[static_cast<int>(g_page)];
    const int grid_w = g_win_w - 2 * g_margin;
    const int rows = rowsOf(page.count, page.columns);

    for (int r = 0; r < rows; r++) {
        lv_obj_t *row = createPane(g_footer_bar);
        lv_obj_set_size(row, grid_w, g_btn_h);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, g_margin, 0);

        // 本行的槽位固定是 columns 个: 末行不满时补**透明占位**, 让已有的按钮
        // 保持等宽。不补的话 flex_grow 会把末行 2 个按钮撑成半屏宽, 和上一行
        // 对不齐(就是待办里"两行宽度不等"那个老问题)。
        const int first = r * page.columns;
        for (int i = first; i < first + page.columns; i++) {
            if (i < page.count) {
                lv_obj_t *btn = createButton(row, page.buttons[i], onAction);
                lv_obj_set_height(btn, g_btn_h);
                lv_obj_set_flex_grow(btn, 1);
            } else {
                lv_obj_t *slot = createPane(row);  // 透明占位: 只占宽度, 不显示
                lv_obj_set_height(slot, g_btn_h);
                lv_obj_set_flex_grow(slot, 1);
            }
        }
    }
    refreshPageLabel();
}

void buildUi(int win_w, int win_h)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(kColBg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    g_win_w = win_w;
    g_margin = pct(win_w, 1);

    // 底栏按钮高度。触摸屏上手指触点约 40~50 px, 原来取屏高 9%(1024x600 上 54 px)
    // —— 实测**手感偏紧, 不好按**, 所以提到 13%: 同一块屏上是 78 px(高了 44%)。
    // 上下限挡住极端窗口尺寸: 最小 48(再小就真的难按), 最大 96(再高会把内容区挤没)。
    g_btn_h = pct(win_h, 13);
    if (g_btn_h < 48) {
        g_btn_h = 48;
    }
    if (g_btn_h > 96) {
        g_btn_h = 96;
    }

    const int header_h = pct(win_h, 11);

    buildHeader(scr, win_w, header_h, g_margin);
    const int footer_h = buildFooter(scr, win_w, g_btn_h, g_margin);
    buildContent(scr, win_w, header_h, win_h - header_h - footer_h);
    buildPageButtons();  // 首页
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
            // 深底上用强调色, 别用主题默认色
            lv_obj_set_style_text_color(cur, lv_color_hex(kColAccent), 0);
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

    // 窗口尺寸按屏幕实际尺寸算。
    //
    // 板上确认跑着 **Openbox** (桌面环境 LXDE, 见 README「板上运行环境」), 所以
    // 这个窗口会被 WM 接管: 开发机上实测窗口被加了装饰、报了 1074x687 而不是
    // 请求的 1024x600。要真正铺满屏幕, 需要在建窗口后发
    // _NET_WM_STATE_FULLSCREEN (走 EWMH, 见 WARNING.md C-7)。
    const int win_w = windowed ? kBoardWidth : screen_w;
    const int win_h = windowed ? kBoardHeight : screen_h;
    std::printf("  窗口   : %dx%d%s\n", win_w, win_h,
                windowed ? " (窗口模式)" : " (全屏)");

    // -----------------------------------------------------------------------
    // LVGL 初始化
    // -----------------------------------------------------------------------
    lv_init();

    // FreeType 已经由 lv_init() 初始化过了 (third_party/lvgl/src/lv_init.c:
    // lv_freetype_init(LV_FREETYPE_CACHE_FT_GLYPH_CNT))。**不要再调一次** ——
    // 第二次会因为 ft_ctx 已存在而返回 LV_RESULT_INVALID, 于是这里必然误报
    // "FreeType 初始化失败, 中文可能显示为方框", 而中文其实是正常的。板上日志
    // 里同样会出现那句, 会把上板验收引向错误方向。
    // 缓存大小要调就改 lv_conf.h 的 LV_FREETYPE_CACHE_FT_GLYPH_CNT; 字形到底
    // 能不能用, 由 createFont() 的失败分支负责报错。

    const char *font_file = pickFontFile(font_override);
    if (font_file != nullptr) {
        g_font_big = createFont(font_file, kBigFontPx, "主标题");
        g_font_small = createFont(font_file, kSmallFontPx, "按钮标签");
        g_font_status = createFont(font_file, kStatusFontPx, "状态文字");
    }

    g_disp = lv_x11_window_create(kTitle, win_w, win_h);
    if (g_disp == nullptr) {
        std::fprintf(stderr, "创建 X11 窗口失败\n");
        return 1;
    }
    lv_x11_inputs_create(g_disp, nullptr);
    lv_display_add_event_cb(g_disp, onDisplayDeleted, LV_EVENT_DELETE, nullptr);

    // 输入采样压得比刷新快: 刷新 16ms 一次决定"画面多久更新", 采样 5ms 一次决定
    // "多久看一眼有没有按下"。两者用同一个值会让快点击被整次吞掉(见 kIndevReadMs)。
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev != nullptr;
         indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_timer_t *timer = lv_indev_get_read_timer(indev);
            if (timer != nullptr) {
                lv_timer_set_period(timer, kIndevReadMs);
            }
        }
    }

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

    std::printf("  布局   : 深色工业风 —— 顶栏(品牌/状态灯/退出) + 内容区 + 分页底栏"
                " (%d 页, 首页 %d 个按钮)\n",
                countOf(kPages), kPages[static_cast<int>(Page::Home)].count);
    std::printf("\n交互:\n");
    std::printf("  点右上角【退出】   退出程序\n");
    std::printf("  首页点【LED 调试】/【蜂鸣器 调试】/【外部无源蜂鸣器】 进对应调试页,\n");
    std::printf("                    页内末位【返回】回首页\n");
    std::printf("  调试页里点按钮     单独控制板载 LED / 蜂鸣器 / 外部无源蜂鸣器\n");
    std::printf("  按 q / Esc        退出程序 (每一页都一样)\n");
    std::fflush(stdout);

    g_start_tick = lv_tick_get();

    while (!g_quit) {
        // 外部无源蜂鸣器: 开关标志在 onAction() 里改, 这里每轮**推进一次方波**
        // (buzzer_out_tick() 内部按时间翻转 GPIOA:6, 不是"每轮发一个脉冲")。
        // 关着时它立刻返回, 没有开销; 板上没接这个蜂鸣器时返回 -1, 不影响循环。
        buzzer_out_tick();

        // 切页在这里落地: 事件回调只置标志, 避免在派发点击的过程中删掉底栏
        // (见 g_page_dirty 的说明)。重建完刷新内容区那行小字。
        if (g_page_dirty) {
            g_page_dirty = false;
            buildPageButtons();
            std::printf("切换到 %s (底栏 %d 个按钮)\n",
                        kPages[static_cast<int>(g_page)].name,
                        kPages[static_cast<int>(g_page)].count);
            std::fflush(stdout);
        }

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
    if (g_font_status != nullptr) {
        lv_freetype_font_delete(g_font_status);
    }
    lv_deinit();
    return 0;
}
