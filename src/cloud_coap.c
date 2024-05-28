#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <net/nrf_cloud_coap.h>
#include <cJSON.h>

#include "cloud.h"

LOG_MODULE_REGISTER(cloud, CONFIG_CLOUD_DISPLAY_LOG_LEVEL);

static void shadow_poll_timer_handler(struct k_timer *id);

K_SEM_DEFINE(shadow_poll_sem, 0, 1);
K_TIMER_DEFINE(shadow_poll_timer, shadow_poll_timer_handler, NULL);

char shadow_buffer[CONFIG_SHADOW_BUFFER_SIZE];

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

    LOG_DBG("Shadow:\n%s", cJSON_Print(input));

    cJSON *config = cJSON_GetObjectItem(input, "config");
    if (config == NULL) {
        LOG_ERR("Could not find config object");
        return -1;
    }
    
    cJSON *display = cJSON_GetObjectItem(config, "display");
    if (display == NULL) {
        LOG_ERR("Could not find display config");
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
        LOG_ERR("Failed to create root object");
        return -1;
    }

    cJSON *config = cJSON_CreateObject();
    if (config == NULL) {
        LOG_ERR("Failed to create config object");
        return -1;
    }

    cJSON_AddItemToObject(root, "config", config);

    if (cJSON_AddStringToObject(config, "display", display_string) == NULL) {
        LOG_ERR("Failed to add display string");
        cJSON_Delete(root);
        return -1;
    }

    *json = cJSON_Print(root);
    if (json == NULL) {
        LOG_ERR("Failed to print JSON string");
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

int cloud_thread(void)
{
    int err;

    LOG_INF("Cloud thread started");
    
    err = nrf_cloud_coap_init();
    if (err) {
        LOG_ERR("Failed to init nRF Cloud CoAP: %d", err);
        return err;
    }

    err = nrf_cloud_coap_connect(NULL);
    if (err) {
        LOG_ERR("Failed to connect to nRF Cloud");
        return err;
    }

    LOG_DBG("Setting initial config");
    char *json = NULL;
    err = encode_config("", &json);
    if (err || json == NULL) {
        LOG_ERR("Failed to encode default config. err: %d, json: %p", err, json);
        return err;
    }

    err = nrf_cloud_coap_shadow_state_update(json);
    if (err) {
        LOG_ERR("Failed to update shadow state: %d", err);
        return err;
    }
    free(json);
    json = NULL;

    k_timer_start(&shadow_poll_timer, K_NO_WAIT, K_SECONDS(CONFIG_CLOUD_POLL_INTERVAL));

    while (true) {
        k_sem_take(&shadow_poll_sem, K_FOREVER);

        LOG_INF("Getting shadow");
        err = nrf_cloud_coap_shadow_get(shadow_buffer, CONFIG_SHADOW_BUFFER_SIZE, true);
        if (err) {
            LOG_ERR("Failed to get shadow delta: %d", err);
            continue;
        } else if (err == 0 && strlen(shadow_buffer) == 0) {
            LOG_INF("No changes to the shadow");
            continue;
        }

        LOG_DBG("Shadow delta:\n%s\n", shadow_buffer);

        struct nrf_cloud_data shadow_delta = {
            .ptr = shadow_buffer,
            .len = strlen(shadow_buffer)
        };
        struct nrf_cloud_obj shadow_config;

        err = nrf_cloud_coap_shadow_delta_process(&shadow_delta, &shadow_config);
        if (err) {
            LOG_ERR("Failed to process shadow delta: %d", err);
            goto shadow_cleanup;
        } else if (shadow_config.type != NRF_CLOUD_OBJ_TYPE_JSON) {
            LOG_ERR("Unsupported nRF Cloud object type: %d", shadow_config.type);
            goto shadow_cleanup;
        }

        char *string = NULL;
        err = parse_config(shadow_config.json, &string);
        if (err || string == NULL) {
            LOG_ERR("Failed to parse shadow: %d", err);
            goto shadow_cleanup;
        }
        LOG_DBG("Display: %s", string);

        // Do something with the string

        // Set the string written to the display in the "reported" part of the shadow
        err = encode_config(string, &json);
        if (err || json == NULL) {
            LOG_ERR("Failed to encode default config");
            continue;
        }

        err = nrf_cloud_coap_shadow_state_update(json);
        if (err) {
            LOG_ERR("Failed to update shadow state: %d", err);
            goto json_cleanup;
        }

json_cleanup:
        free(json);
        json = NULL;
        
shadow_cleanup:
        err = nrf_cloud_obj_free(&shadow_config);
        if (err) {
            LOG_ERR("Failed to free shadow object: %d", err);
            continue;
        }
    }

    return 0;
}