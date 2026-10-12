/* ============================================================================
 * BSP/KEY/key.c 的行为测试台 (在 PC 上跑, 不进固件)
 *
 * 把真实的 key.c 编进来 (不是复制一份逻辑), 只提供 HAL 的假实现:
 *   - 假引脚状态 + 假时钟, 由下面的 test_* 用例驱动
 * 然后断言 Key_Scan() 吐出的事件序列。
 *
 * 编译运行见 _syntax_check/run_key_test.ps1
 * ========================================================================== */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "key.h"

/* ------------------------------ 替身: HAL ------------------------------ */
static uint8_t  g_Level   = 1U;      /* 当前引脚电平: 1=松开, 0=按下 */
static uint32_t g_Now     = 0U;      /* 假时钟, 单位 ms */

uint32_t HAL_GetTick(void)                     { return g_Now; }
void     HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g) { (void)p; (void)g; }
void     HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState s) { (void)p; (void)pin; (void)s; }

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port;
    (void)pin;
    return (g_Level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/* ------------------------------ 测试工具 ------------------------------ */
static int g_Fail = 0;

static const char *EvName(Key_Event e)
{
    switch (e)
    {
        case KEY_EVENT_NONE:    return "NONE";
        case KEY_EVENT_PRESS:   return "PRESS";
        case KEY_EVENT_LONG:    return "LONG";
        case KEY_EVENT_RELEASE: return "RELEASE";
        default:                return "?";
    }
}

/* 走 n 个扫描周期, 每个周期把电平设为上面的 g_Level, 收集非 NONE 的事件 */
static void Run(const char *tag, int cycles, Key_Event *got, int *n)
{
    int i;
    *n = 0;
    for (i = 0; i < cycles; i++)
    {
        Key_Event e = Key_Scan(KEY_ID_1);
        if (e != KEY_EVENT_NONE)
        {
            if (*n < 16) { got[(*n)++] = e; }
        }
        g_Now += KEY_SCAN_MS;
    }
    printf("  %-34s -> ", tag);
    if (*n == 0) { printf("(无事件)"); }
    for (i = 0; i < *n; i++) { printf("%s ", EvName(got[i])); }
    printf("\n");
}

static void Check(const char *what, const Key_Event *got, int n, const Key_Event *want, int wn)
{
    int i;
    int ok = (n == wn);
    for (i = 0; ok && i < n; i++) { if (got[i] != want[i]) { ok = 0; } }
    if (!ok)
    {
        g_Fail++;
        printf("    [FAIL] %s: 期望", what);
        for (i = 0; i < wn; i++) { printf(" %s", EvName(want[i])); }
        printf("  实际");
        for (i = 0; i < n; i++) { printf(" %s", EvName(got[i])); }
        printf("\n");
    }
    else
    {
        printf("    [ok]   %s\n", what);
    }
}

/* 让静态状态回到初始: 松开电平连续跑几个周期 */
static void Reset(void)
{
    int i;
    g_Level = 1U;
    for (i = 0; i < 4; i++) { (void)Key_Scan(KEY_ID_1); g_Now += KEY_SCAN_MS; }
}

int main(void)
{
    Key_Event got[16];
    int n;

    printf("=== key.c 行为测试 ===\n\n");

    /* --- 1. 短按一次: 只应出现 1 个 PRESS --- */
    printf("[1] 短按一次 (按下 3 个周期后松开)\n");
    Reset();
    g_Level = 0U;
    Run("按下 3 周期", 3, got, &n);
    {
        Key_Event want[] = { KEY_EVENT_PRESS };
        Check("短按只报一次 PRESS", got, n, want, 1);
    }
    g_Level = 1U;
    Run("松开 3 周期", 3, got, &n);

    /* --- 2. 按住不放: 1 个 PRESS + 1 个 LONG, 不能重复 --- */
    printf("\n[2] 按住 2 秒不放 (100 个周期)\n");
    Reset();
    g_Level = 0U;
    Run("按住 100 周期", 100, got, &n);
    {
        Key_Event want[] = { KEY_EVENT_PRESS, KEY_EVENT_LONG };
        Check("PRESS 一次 + LONG 一次", got, n, want, 2);
    }
    g_Level = 1U;
    Run("松开", 3, got, &n);
    {
        Key_Event want[] = { KEY_EVENT_RELEASE };
        Check("长按后松手只报 RELEASE (不补短按)", got, n, want, 1);
    }

    /* --- 3. 连续按三下: 应该正好 3 个 PRESS --- */
    printf("\n[3] 连按三下\n");
    Reset();
    {
        int k;
        int presses = 0;
        for (k = 0; k < 3; k++)
        {
            g_Level = 0U;
            Run("按下", 3, got, &n);
            { int i; for (i = 0; i < n; i++) { if (got[i] == KEY_EVENT_PRESS) { presses++; } } }
            g_Level = 1U;
            Run("松开", 3, got, &n);
        }
        if (presses == 3) { printf("    [ok]   三下 = 3 个 PRESS\n"); }
        else { g_Fail++; printf("    [FAIL] 三下报了 %d 个 PRESS\n", presses); }
    }

    /* --- 4. 抖动: 按下前有毛刺, 不能误报 --- */
    printf("\n[4] 抖动 (松-按-松-按 ... 各 1 个周期, 最后稳定按下)\n");
    Reset();
    g_Level = 0U; Run("毛刺1 按下", 1, got, &n);
    {
        Key_Event want[1];          /* 期望: 一个事件都不报 */
        Check("单周期抖动不报事件", got, n, want, 0);
    }
    g_Level = 1U; Run("毛刺1 松开", 1, got, &n);
    g_Level = 0U; Run("毛刺2 按下", 1, got, &n);
    g_Level = 1U; Run("毛刺2 松开", 1, got, &n);
    g_Level = 0U; Run("稳定按下", 3, got, &n);
    {
        Key_Event want[] = { KEY_EVENT_PRESS };
        Check("抖动之后只报 1 个 PRESS", got, n, want, 1);
    }
    g_Level = 1U; Run("最终松开", 3, got, &n);

    /* --- 5. Key_GetNum: 参考工程那种用法 --- */
    printf("\n[5] Key_GetNum KEY_ID_3 (按下应返回 3)\n");
    Reset();
    g_Level = 0U;
    {
        uint8_t num = 0U;
        int i;
        for (i = 0; i < 3; i++)
        {
            uint8_t v = Key_GetNum(KEY_ID_3);
            if (v != 0U) { num = v; }
            g_Now += KEY_SCAN_MS;
        }
        if (num == 3U) { printf("    [ok]   返回 3\n"); }
        else { g_Fail++; printf("    [FAIL] 返回 %u\n", (unsigned)num); }
    }
    g_Level = 1U;
    {
        uint8_t v;
        int i;
        for (i = 0; i < 3; i++) { v = Key_GetNum(KEY_ID_3); g_Now += KEY_SCAN_MS; }
        if (v == 0U) { printf("    [ok]   松开时返回 0\n"); }
        else { g_Fail++; printf("    [FAIL] 松开时返回 %u\n", (unsigned)v); }
    }

    /* --- 6. 边界: 按住差一点到 1 秒, 不能报长按 --- */
    printf("\n[6] 按住 940ms (47 个周期, 差 3 个周期不到 1 秒)\n");
    Reset();
    g_Level = 0U;
    Run("按住 47 周期", 47, got, &n);
    {
        Key_Event want[] = { KEY_EVENT_PRESS };
        Check("940ms 只报 PRESS, 不报 LONG", got, n, want, 1);
    }
    g_Level = 1U;
    Run("松开", 3, got, &n);
    {
        Key_Event want[] = { KEY_EVENT_RELEASE };
        Check("松手报 RELEASE", got, n, want, 1);
    }

    /* --- 7. 边界: 按住 1100ms 必须报长按 --- */
    printf("\n[7] 按住 1100ms (55 个周期)\n");
    Reset();
    g_Level = 0U;
    Run("按住 55 周期", 55, got, &n);
    {
        Key_Event want[] = { KEY_EVENT_PRESS, KEY_EVENT_LONG };
        Check("1100ms 报 PRESS + LONG", got, n, want, 2);
    }
    g_Level = 1U;
    Run("松开", 3, got, &n);

    /* --- 8. 非法 id 不能崩 --- */
    printf("\n[8] 非法编号\n");
    {
        Key_Event e = Key_Scan(99U);
        if (e == KEY_EVENT_NONE) { printf("    [ok]   Key_Scan(99) 返回 NONE\n"); }
        else { g_Fail++; printf("    [FAIL] Key_Scan(99) 返回 %s\n", EvName(e)); }
    }

    printf("\n=== %s (%d failed) ===\n", g_Fail ? "FAIL" : "PASS", g_Fail);
    return g_Fail ? 1 : 0;
}
