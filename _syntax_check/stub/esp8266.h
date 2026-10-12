#ifndef __STUB_ESP8266_H
#define __STUB_ESP8266_H
#include "main.h"
#include <stdint.h>
#define ESP8266_OK 1u
#define ESP8266_ERR 0u
uint8_t ESP8266_Init(void);
uint8_t ESP8266_ConnectAP(const char *ssid, const char *pwd);
uint8_t ESP8266_MQTT_SetParam(const char *cid, const char *user,
                              const char *pass, uint16_t keepalive);
uint8_t ESP8266_MQTT_ConnectBroker(const char *host, uint16_t port);
uint8_t ESP8266_MQTT_Publish(const char *topic, const char *payload);
#endif
