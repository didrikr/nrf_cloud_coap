#ifndef __CLOUD_H__
#define __CLOUD_H__

#include <zephyr/kernel.h>

#if defined(CONFIG_NRF_CLOUD_MQTT)
    #define MODE "MQTT"
#elif defined(CONFIG_NRF_CLOUD_COAP)
    #define MODE "COAP"
#else
    #error "Unsupported nRF Cloud protocol"
#endif

#define MESSAGE MODE " test"
#define APP_ID "custom"

int cloud_init();

int cloud_send(uint8_t *buf, size_t len);

#endif /* __CLOUD_H__ */