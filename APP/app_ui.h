#ifndef __APP_UI_H
#define __APP_UI_H

#include <stdint.h>

/* ============================================================================
 * 界面状态 —— 时钟 + 阈值 + 菜单 + 当前页面
 *
 * 这四样都是"界面上的状态": 按键任务改它们, OLED 任务读它们。
 * 合在一个模块里, 是因为它们互相咬得很紧 ——
 * 阈值要靠菜单来改, 页面决定画哪一屏。
 * 拆成三个文件只会让 include 链互相绕。
 *
 * 数据流是单向的: 按键任务 write-only, OLED 任务 read-only。
 * 所以这里全部用 volatile 就够, 不需要任何锁。
 * (唯一例外是时钟的秒脉冲来自 OLED 任务自己, 见 AppClock_Tick)
 * ========================================================================== */

/* ============================================================================
 * 一、时钟 —— WiFi 联网对时 + 软件走字, 板上没有 RTC
 *
 * 时间源头: WiFi 任务联网成功后调 AppClock_SetTime() 写入 (见 task_wifi.c);
 * 秒进位:   OLED 任务周期正好 1 秒, 调 AppClock_Tick 往前走。
 * 断电重启先显示 00:00:00, WiFi 连上热点才跳回真实时间。
 * ========================================================================== */

typedef struct {
    uint8_t hour;       /* 0~23 */
    uint8_t min;        /* 0~59 */
    uint8_t sec;        /* 0~59 */
} Clock_Time;

void       AppClock_Tick(void);                        /* 每过 1 秒调一次 */
Clock_Time AppClock_Now(void);
void       AppClock_SetTime(uint8_t hour, uint8_t min, uint8_t sec); /* 联网对时 */

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
 * 三、菜单 —— 一条平铺的列表, 6 项依次是 6 个阈值 (温/湿/光 各上下限)
 *
 *    菜单页显示的是 ASCII 项名 (TempHi/...), 见 Menu_Name();
 *    中文标签只在需要的时候另外画 (task_oled.c 的菜单页)。
 * ========================================================================== */

#define MENU_COUNT      TH_COUNT                /* 一共 6 项 */

extern volatile uint8_t g_xMenuIdx;

const char *Menu_Name(uint8_t idx);
const char *Menu_Unit(uint8_t idx);
int16_t     Menu_Value(uint8_t idx);
void        Menu_Adj(uint8_t idx, int8_t dir);

/* ============================================================================
 * 四、页面 —— 按键任务切页, OLED 任务照着画
 *
 *   显示页  首页(WiFi) -> 温度 -> 湿度 -> 光照, KEY2/KEY3 循环切页
 *   菜单页  设置面: 列 6 项阈值, KEY2/KEY3 上下选, KEY4 进编辑
 *   编辑页  改选中项的值, KEY2/KEY3 加减
 * ========================================================================== */

#define UI_PAGE_ENV     0U      /* 首页: WiFi 连接状态 */
#define UI_PAGE_TEMP    1U      /* 温度显示 */
#define UI_PAGE_HUMI    2U      /* 湿度显示 */
#define UI_PAGE_LUX     3U      /* 光照显示 */
#define UI_PAGE_MENU    4U      /* 设置面第二级: 阈值列表 */
#define UI_PAGE_EDIT    5U      /* 编辑: 改选中项的值 */
#define UI_PAGE_SET     6U      /* 设置面第一级: 分类选择 */

extern volatile uint8_t g_xUiPage;

/* 设置面第一级: 温度 / 湿度 / 光照 / 阈值设置 */
#define SET_TEMP    0U
#define SET_HUMI    1U
#define SET_LUX     2U
#define SET_THRESH  3U
#define SET_COUNT   4U

extern volatile uint8_t g_xSetIdx;

const char *Set_Name(uint8_t idx);

/* 当前告警码 (0 = 正常, 其余见 asr.h 的 ASR_ALARM_xxx)。
 * **只由语音任务算** (每秒一次) 写进来, OLED 只读 —— 只算一处, 两边才不会打架。
 * OLED 任务拿它做"超限闪烁提示" (必做4)。 */
extern volatile uint8_t g_xAlarmCode;

/* 用 app_config.h 里的宏给阈值填初值。在 App_Init() 里调 */
void UI_Init(void);

#endif /* __APP_UI_H */
