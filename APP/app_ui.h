#ifndef __APP_UI_H
#define __APP_UI_H

#include <stdint.h>

/* ============================================================================
 * 界面状态 —— 时钟 + 阈值 + 菜单 + 当前页面
 *
 * 这四样都是"界面上的状态": 按键任务改它们, OLED 任务读它们。
 * 合在一个模块里, 是因为它们互相咬得很紧 ——
 * 菜单的第 0 项就是时钟的小时, 阈值要靠菜单来改, 页面决定画哪一屏。
 * 拆成三个文件只会让 include 链互相绕。
 *
 * 数据流是单向的: 按键任务 write-only, OLED 任务 read-only。
 * 所以这里全部用 volatile 就够, 不需要任何锁。
 * (唯一例外是时钟的秒脉冲来自 OLED 任务自己, 见 AppClock_Tick)
 * ========================================================================== */

/* ============================================================================
 * 一、模拟时钟 —— 软件累加, 板上没有 RTC
 *
 * ⚠️ 只是"看起来在走"的占位实现: 断电归零, 而且精度全靠调用者的节拍
 *    (OLED 任务周期正好 1 秒, 就拿它当秒脉冲)。
 *    以后接真时间, 换掉 AppClock_Now() 的实现即可, 界面代码一行都不用动:
 *      a) 上电计时    —— 零成本, 会漂
 *      b) 网络对时    —— ESP8266 连上热点后取一次
 *      c) 外挂 RTC    —— DS3231/PCF8563 走 I2C, 断电不丢
 * ========================================================================== */

typedef struct {
    uint8_t hour;       /* 0~23 */
    uint8_t min;        /* 0~59 */
    uint8_t sec;        /* 0~59 */
} Clock_Time;

void       AppClock_Init(uint8_t hour, uint8_t min);   /* 在 App_Init() 里调一次 */
void       AppClock_Tick(void);                        /* 每过 1 秒调一次 */
Clock_Time AppClock_Now(void);

uint8_t AppClock_GetHour(void);                        /* 菜单第 0 项改的就是它 */
void    AppClock_SetHour(uint8_t hour);

/* ============================================================================
 * 二、阈值 —— 运行时变量
 *
 *    TH_TEMP_UP 温度上限 ℃      TH_TEMP_DN 温度下限 ℃
 *    TH_HUMI_UP 湿度上限 %RH     TH_HUMI_DN 湿度下限 %RH
 *    TH_LUX_DN  光照下限 lx      TH_LUX_UP  光照上限 lx
 *
 * ⚠️ 现在只存在 RAM 里 —— 断电回默认值。要断电不丢, 得往 Flash 存一份。
 * ========================================================================== */

typedef enum {
    TH_TEMP_UP = 0,
    TH_TEMP_DN,
    TH_HUMI_UP,
    TH_HUMI_DN,
    TH_LUX_DN,
    TH_LUX_UP,
    TH_COUNT
} Thresh_Id;

extern volatile int16_t g_xThresh[TH_COUNT];

int16_t Thresh_Get(Thresh_Id id);
void    Thresh_Adj(Thresh_Id id, int8_t dir);   /* dir = +1 / -1, 自动夹紧 */

/* ============================================================================
 * 三、菜单 —— 一条平铺的列表, 第 0 项是时间, 后面 6 项依次是 6 个阈值
 *
 *    菜单页显示的是 ASCII 项名 (Time/TempHi/...), 见 Menu_Name();
 *    中文标签只在需要的时候另外画 (task_oled.c 的菜单页)。
 * ========================================================================== */

#define MENU_TIME       0U
#define MENU_COUNT      (TH_COUNT + 1U)         /* 一共 7 项 */

extern volatile uint8_t g_xMenuIdx;

const char *Menu_Name(uint8_t idx);
const char *Menu_Unit(uint8_t idx);
int16_t     Menu_Value(uint8_t idx);
void        Menu_Adj(uint8_t idx, int8_t dir);

/* ============================================================================
 * 四、页面 —— 按键任务切页, OLED 任务照着画
 *
 *   主页面  温度 / 湿度 / 光照 / 时间 都在这一屏 (必做2 要求的四项)
 *   菜单页  列 7 项, KEY2/KEY3 上下选
 *   编辑页  改选中项的值
 * ========================================================================== */

#define UI_PAGE_ENV     0U      /* 主页面: 温度 + 湿度 + 光照 + 时间 */
#define UI_PAGE_MENU    1U      /* 菜单: 列表, KEY2/KEY3 上下移动 */
#define UI_PAGE_EDIT    2U      /* 编辑: 改选中项的值 */

extern volatile uint8_t g_xUiPage;

/* 当前告警码 (0 = 正常, 其余见 asr.h 的 ASR_ALARM_xxx)。
 * **只由语音任务算** (每秒一次) 写进来, OLED 只读 —— 只算一处, 两边才不会打架。
 * OLED 任务拿它做"超限闪烁提示" (必做4)。 */
extern volatile uint8_t g_xAlarmCode;

/* 用 app_config.h 里的宏给阈值填初值。在 App_Init() 里调 */
void UI_Init(void);

#endif /* __APP_UI_H */
