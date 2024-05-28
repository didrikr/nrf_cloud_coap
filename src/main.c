#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>
#include <net/nrf_provisioning.h>
#include <zephyr/sys/reboot.h>
#include <cJSON.h>

#include "nrf_provisioning_at.h"

#include "cloud.h"

LOG_MODULE_REGISTER(main, CONFIG_CLOUD_DISPLAY_LOG_LEVEL); 


K_SEM_DEFINE(lte_connected, 0, 1);
K_SEM_DEFINE(provisioning_complete, 0, 1);

static struct nrf_provisioning_dm_change dmode;
static struct nrf_provisioning_mm_change mmode;

static void lte_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		    (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}

		LOG_INF("Network registration status: %s",
		       evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
		       "Connected - home network" : "Connected - roaming");
		k_sem_give(&lte_connected);
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_INF("RRC mode: %s",
		       evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
		       "Connected" : "Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_INF("LTE cell changed: Cell ID: %d, Tracking area: %d\n",
		       evt->cell.id, evt->cell.tac);
		break;
	case LTE_LC_EVT_MODEM_EVENT:
		LOG_INF("Modem event: %d", evt->modem_evt);
		break;
	default:
		break;
	}
}

static int modem_mode_cb(enum lte_lc_func_mode new_mode, void *user_data)
{
	enum lte_lc_func_mode fmode;
	char time_buf[64];
	int ret;

	ARG_UNUSED(user_data);

	if (lte_lc_func_mode_get(&fmode)) {
		LOG_ERR("Failed to read modem functional mode");
		ret = -EFAULT;
		return ret;
	}

	if (fmode == new_mode) {
		ret = fmode;
	} else if (new_mode == LTE_LC_FUNC_MODE_NORMAL) {
		/* Use the blocking call, because in next step
		 * the service will create a socket and call connect()
		 */
		ret = lte_lc_connect();

		if (ret) {
			LOG_ERR("lte_lc_connect() failed %d", ret);
		}
		LOG_INF("Modem connection restored");

		LOG_INF("Waiting for modem to acquire network time...");

		do {
			k_sleep(K_SECONDS(3));
			ret = nrf_provisioning_at_time_get(time_buf, sizeof(time_buf));
		} while (ret != 0);

		LOG_INF("Network time obtained");
		ret = fmode;
	} else {
		ret = lte_lc_func_mode_set(new_mode);
		if (ret == 0) {
			LOG_INF("Modem set to requested state %d", new_mode);
			ret = fmode;
		}
	}

	return ret;
}

static void reboot_device(void)
{
	/* Disconnect from network gracefully */
	int ret = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_OFFLINE);

	if (ret != 0) {
		LOG_ERR("Unable to set modem offline, error %d", ret);
	}

	sys_reboot(SYS_REBOOT_WARM);
}

static void device_mode_cb(enum nrf_provisioning_event event, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (event) {
	case NRF_PROVISIONING_EVENT_START:
		LOG_ERR("Provisioning started");
		break;
	case NRF_PROVISIONING_EVENT_STOP:
		LOG_ERR("Provisioning stopped");
		k_sem_give(&provisioning_complete);
		break;
	case NRF_PROVISIONING_EVENT_DONE:
		LOG_ERR("Provisioning done, rebooting...");
		reboot_device();
		break;
	default:
		LOG_ERR("Unknown event");
		break;
	}
}

int main(void)
{
    int err;

    mmode.cb = modem_mode_cb;
		mmode.user_data = NULL;
		dmode.cb = device_mode_cb;
		dmode.user_data = NULL;

    LOG_INF("nRF Cloud CoAP display demo started");
    LOG_INF("Polling interval: %d minutes", CONFIG_CLOUD_POLL_INTERVAL);

    err = nrf_modem_lib_init();
    if (err) {
        LOG_ERR("Modem initialization failed: %d", err);
        return 1;
    }

    err = lte_lc_connect_async(lte_handler);
    if (err) {
        LOG_ERR("Failed to start network connection: %d", err);
        return 1;
    }

    k_sem_take(&lte_connected, K_FOREVER);

		err = nrf_provisioning_init(&mmode, &dmode);
		if (err) {
			LOG_ERR("Failed to initialize provisioning client");
		}

		k_sem_take(&provisioning_complete, K_FOREVER);
		LOG_ERR("Provisioning complete");

		cloud_thread();
    return 0;
}
