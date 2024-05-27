#include <zephyr/kernel.h>

#include <net/nrf_cloud_coap.h>
#include <cJSON.h>

#include "cloud.h"

#define POLL_INTERVAL 60
#define SHADOW_BUFFER_SIZE 1024

void shadow_poll_timer_handler(struct k_timer *id);

K_SEM_DEFINE(shadow_poll_sem, 0, 1);
K_TIMER_DEFINE(shadow_poll_timer, shadow_poll_timer_handler, NULL);

char shadow_buffer[SHADOW_BUFFER_SIZE];

void shadow_poll_timer_handler(struct k_timer *id)
{
    k_sem_give(&shadow_poll_sem);
}

int cloud_thread(void)
{
    int err;

    printk("Cloud thread started\n");
    
    err = nrf_cloud_coap_init();
    if (err) {
        printk("Failed to init nRF Cloud CoAP: %d\n", err);
        return err;
    }

    err = nrf_cloud_coap_connect(NULL);
    if (err) {
        printk("Failed to connect to nRF Cloud\n");
        return err;
    }

    printk("Setting initial config\n");
    err = nrf_cloud_coap_shadow_state_update("{\"config\":{\"display\":\"\"}}");
    if (err) {
        printk("Failed to update shadow state: %d\n", err);
        return err;
    }

    k_timer_start(&shadow_poll_timer, K_NO_WAIT, K_SECONDS(POLL_INTERVAL));

    while (true) {
        k_sem_take(&shadow_poll_sem, K_FOREVER);

        printk("Getting shadow\n");
        err = nrf_cloud_coap_shadow_get(shadow_buffer, SHADOW_BUFFER_SIZE, true);
        if (err) {
            printk("Failed to get shadow delta: %d\n", err);
            return err;
        }

        printk("Shadow delta:\n%s\n\n", shadow_buffer);

        struct nrf_cloud_data shadow_delta = {
            .ptr = shadow_buffer,
            .len = strlen(shadow_buffer)
        };
        struct nrf_cloud_obj shadow_config;

        err = nrf_cloud_coap_shadow_delta_process(&shadow_delta, &shadow_config);
        if (err) {
            printk("Failed to process shadow delta: %d\n", err);
            return err;
        } else if (shadow_config.type != NRF_CLOUD_OBJ_TYPE_JSON) {
            printk("Unsupported nRF Cloud object type: %d\n", shadow_config.type);
        }

        printk("Shadow config type: %d\n", shadow_config.type);
        cJSON_Print(shadow_config.json);

        cJSON *config = cJSON_GetObjectItem(shadow_config.json, "config");
        if (config == NULL) {
            printk("Could not find config object\n");
            continue;
        }
       
        cJSON *display = cJSON_GetObjectItem(config, "display");
        if (display == NULL) {
            printk("Could not find display config\n");
            continue;
        }

        char *string = cJSON_GetStringValue(display);
        printk("Display: %s\n", string);

        // Do something with the string

        // Set the string written to the display in the "reported" part of the shadow

        err = nrf_cloud_obj_free(&shadow_config);
        if (err) {
            printk("Failed to free shadow object: %d\n", err);
            continue;
        }
    }

    return 0;
}