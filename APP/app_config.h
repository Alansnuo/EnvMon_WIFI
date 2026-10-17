#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H
/* ============================================================================
 * 部署配置: 所有环境相关参数集中存放
 * 换 WiFi / 云平台只改本文件, 业务代码直接引用宏
 * 仅宏定义, 不包含头文件, 可被任意文件包含
 * ========================================================================== */
/* ========================= WiFi ========================= */
#define CFG_WIFI_SSID         "EEB9-511"
#define CFG_WIFI_PASSWORD     "EEB9-511"

/* ========================= MQTT (OneNET) =========================
 * 使用明文 1883 端口, 不用 TLS (ESP8266 内存不足).
 * ClientID=设备名, Username=产品 ID.
 * Password=OneNET 鉴权 Token, 须整串复制、不可换行; Token 超长,
 * 用 AT+MQTTPASSWORD 单独下发, 不放入 AT+MQTTUSERCFG. */
#define CFG_MQTT_HOST         "mqtts.heclouds.com"
#define CFG_MQTT_PORT         1883
#define CFG_MQTT_CLIENT_ID    "test1"
#define CFG_MQTT_USERNAME     "v9cSbzDFqr"
#define CFG_MQTT_PASSWORD     "version=2018-10-31&res=products%2Fv9cSbzDFqr%2Fdevices%2Ftest1&et=1821308308&method=sha1&sign=9Cyhm1fn76GI5cURLVvT6AV4XCg%3D"
#define CFG_MQTT_KEEPALIVE_S  60

/* ===================== 上报主题 =====================
 * 物模型属性上报主题, 与 JSON 载荷配套.
 * 属性名大小写敏感; 同一 ClientID 不可同时在 MQTTX 与板子在线. */
#define CFG_MQTT_PUB_TOPIC    "$sys/v9cSbzDFqr/test1/thing/property/post"

/* ===================== 阈值初值 (必做4: 超限告警) =====================
 * 由 UI_Init() 拷进运行时变量 g_xThresh, 按键可改 (存 RAM, 掉电恢复默认).
 * 光照阈值另用于分档, 决定模块播报"光线充足/偏暗/很暗". */
#define CFG_TH_TEMP_UP        35        /* ℃   高于此值 → 温度过高 */
#define CFG_TH_TEMP_DN        10        /* ℃   低于此值 → 温度过低 */
#define CFG_TH_HUMI_UP        80        /* %RH 高于此值 → 湿度过高 */
#define CFG_TH_HUMI_DN        30        /* %RH 低于此值 → 湿度过低 */
#define CFG_TH_LUX_DN         10        /* lx  低于此值 → 光照不足(光线偏暗) */
#define CFG_TH_LUX_UP         200       /* lx  高于此值 → 光照过强 */

#endif /* __APP_CONFIG_H */
