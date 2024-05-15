#include <zephyr/kernel.h>

#include <net/nrf_cloud_coap.h>

#include "cloud.h"

// We don't need to close the connection each time
#define CLOSE_CONNECTION 0

int cloud_init()
{
    int err;
    err = nrf_cloud_coap_init();
    if (err) {
        printk("Failed to init nRF Cloud CoAP: %d\n", err);
        return err;
    }

#if !CLOSE_CONNECTION
    err = nrf_cloud_coap_connect();
    if (err) {
        printk("nRF Cloud CoAP connect failed: %d\n", err);
        return err; 
    }
#endif

    return err;
}

int cloud_send(uint8_t *buf, size_t len)
{
    int err;

#if CLOSE_CONNECTION
    err = nrf_cloud_coap_connect();
    if (err) {
        printk("nRF Cloud CoAP connect failed: %d\n", err);
        return err; 
    }
#endif

    err = nrf_cloud_coap_message_send(APP_ID, MESSAGE, false, 1693858789426);
    if (err) {
        printk("Failed to send message: %d\n", err);
        return err;
    }

#if CLOSE_CONNECTION

    err = nrf_cloud_coap_disconnect();
    if (err) {
        printk("Failed to disconnect CoAP: %d\n", err);
        return err;
    }
#endif

    return 0;
}