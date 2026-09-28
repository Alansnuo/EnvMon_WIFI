#ifndef __APP_JSON_H
#define __APP_JSON_H
/* ============================================================================
 * 上报报文: 感应器的值攒成一份 JSON, 由 WiFi 任务发给 OneNET 物模型
 *
 * 用法: SetXxx() 填值 → Report() 看值变没变 → 变了的报文就在 AppJson_Buf 里
 *       发成功了再 Commit()
 *
 * 加新传感器: 加一个 AppJson_SetXxx() 存值, 再往 app_json.c 的属性段里多写一行
 * ========================================================================== */

#include <stdint.h>

#define JSON_TEXT_SIZE   128            /* 报文缓冲大小, 加属性可能得调大 */

/* 拼好的报文, 已按 AT 指令转义好. Report() 说"变了"之后读它; 只读, 别往里写 */
extern char AppJson_Buf[JSON_TEXT_SIZE];

/* 填入一路感应器的值 */
void AppJson_SetTemp(int temp);
void AppJson_SetHumi(int humi);

/**
 * @brief  比较当前值和上次发成功的那份, 顺便把报文拼进 AppJson_Buf
 * @retval 1 = 值变了, AppJson_Buf 里是能发出去的报文; 0 = 没变, 不用发
 */
uint8_t AppJson_Report(void);

/**
 * @brief  发送成功后调用, 把当前值认作"已上报". 失败就别调 —— 下次 Report()
 *         还会说"变了", 相当于自动重试
 */
void AppJson_Commit(void);

#endif /* __APP_JSON_H */
