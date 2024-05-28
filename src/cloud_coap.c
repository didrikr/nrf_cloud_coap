#include <zephyr/kernel.h>

#include <net/nrf_cloud_coap.h>
#include <cJSON.h>

#include "cloud.h"

#define POLL_INTERVAL 60
#define SHADOW_BUFFER_SIZE 1024

static void shadow_poll_timer_handler(struct k_timer *id);

K_SEM_DEFINE(shadow_poll_sem, 0, 1);
K_TIMER_DEFINE(shadow_poll_timer, shadow_poll_timer_handler, NULL);

char shadow_buffer[SHADOW_BUFFER_SIZE];

static void shadow_poll_timer_handler(struct k_timer *id)
{
    k_sem_give(&shadow_poll_sem);
}

/* display_string will be pointing to the string value inside the cJSON object,
    and will be freed together with input */
static int parse_config(cJSON *input, char **display_string)
{
    if (input == NULL) {
        return -EINVAL;
    }

    printk("Shadow:\n%s\n", cJSON_Print(input));

    cJSON *config = cJSON_GetObjectItem(input, "config");
    if (config == NULL) {
        printk("Could not find config object\n");
        return -1;
    }
    
    cJSON *display = cJSON_GetObjectItem(config, "display");
    if (display == NULL) {
        printk("Could not find display config\n");
        return -1;
    }

    *display_string = cJSON_GetStringValue(display);
    return 0;
}

/* json is an allocated string that must be freed by the caller */
static int encode_config(char *display_string, char **json)
{
    if (display_string == NULL) {
        return -EINVAL;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        printk("Failed to create root object\n");
        return -1;
    }

    cJSON *config = cJSON_CreateObject();
    if (config == NULL) {
        printk("Failed to create config object\n");
        return -1;
    }

    cJSON_AddItemToObject(root, "config", config);

    if (cJSON_AddStringToObject(config, "display", display_string) == NULL) {
        printk("Failed to add display string\n");
        cJSON_Delete(root);
        return -1;
    }

    *json = cJSON_Print(root);
    if (json == NULL) {
        printk("Failed to print JSON string\n");
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
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
    char *json = NULL;
    err = encode_config("", &json);
    if (err || json == NULL) {
        printk("Failed to encode default config. err: %d, json: %x\n", err, json);
        return err;
    }

    err = nrf_cloud_coap_shadow_state_update(json);
    if (err) {
        printk("Failed to update shadow state: %d\n", err);
        return err;
    }
    free(json);
    json = NULL;

    k_timer_start(&shadow_poll_timer, K_NO_WAIT, K_SECONDS(POLL_INTERVAL));

    while (true) {
        k_sem_take(&shadow_poll_sem, K_FOREVER);

        printk("Getting shadow\n");
        err = nrf_cloud_coap_shadow_get(shadow_buffer, SHADOW_BUFFER_SIZE, true);
        if (err) {
            printk("Failed to get shadow delta: %d\n", err);
            continue;
        } else if (err == 0 && strlen(shadow_buffer) == 0) {
            printk("No changes to the shadow\n");
            continue;
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
            goto shadow_cleanup;
        } else if (shadow_config.type != NRF_CLOUD_OBJ_TYPE_JSON) {
            printk("Unsupported nRF Cloud object type: %d\n", shadow_config.type);
            goto shadow_cleanup;
        }

        char *string = NULL;
        err = parse_config(shadow_config.json, &string);
        if (err || string == NULL) {
            printk("Failed to parse shadow: %d\n", err);
            goto shadow_cleanup;
        }
        printk("Display: %s\n", string);

        // Do something with the string

        // Set the string written to the display in the "reported" part of the shadow
        err = encode_config(string, &json);
        if (err || json == NULL) {
            printk("Failed to encode default config\n");
            continue;
        }

        err = nrf_cloud_coap_shadow_state_update(json);
        if (err) {
            printk("Failed to update shadow state: %d\n", err);
            goto json_cleanup;
        }

json_cleanup:
        free(json);
        json = NULL;
        
shadow_cleanup:
        err = nrf_cloud_obj_free(&shadow_config);
        if (err) {
            printk("Failed to free shadow object: %d\n", err);
            continue;
        }
    }

    return 0;
}