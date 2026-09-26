#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H
/* ============================================================================
 * 部署配置：所有环境相关参数集中存放
 * 换WiFi/云平台只修改本文件，业务代码直接引用宏
 * 仅宏定义，不包含头文件，可被任意文件包含
 * ========================================================================== */
/* ========================= WiFi ========================= */
#define CFG_WIFI_SSID         "奈何神明耍诈"
#define CFG_WIFI_PASSWORD     "G123456789"

/* ========================= MQTT (OneNET) =========================
 * 使用明文1883端口，不使用TLS（ESP8266内存不足）
 * ClientID=设备名，Username=产品ID
 * Password=OneNET鉴权Token，整串复制，不可换行
 * Token超长，使用AT+MQTTPASSWORD单独下发，不要放入AT+MQTTUSERCFG
 */
#define CFG_MQTT_HOST         "mqtts.heclouds.com"
#define CFG_MQTT_PORT         1883
#define CFG_MQTT_CLIENT_ID    "test1"
#define CFG_MQTT_USERNAME     "v9cSbzDFqr"
#define CFG_MQTT_PASSWORD     "version=2018-10-31&res=products%2Fv9cSbzDFqr%2Fdevices%2Ftest1&et=1821308308&method=sha1&sign=9Cyhm1fn76GI5cURLVvT6AV4XCg%3D"
#define CFG_MQTT_KEEPALIVE_S  60

/* ===================== 上报主题 =====================
 * 物模型属性上报主题，与JSON载荷配套
 * 属性名大小写敏感；同一设备ClientID不能同时在MQTTX和板子在线
 */
#define CFG_MQTT_PUB_TOPIC    "$sys/v9cSbzDFqr/test1/thing/property/post"

#endif /* __APP_CONFIG_H */
