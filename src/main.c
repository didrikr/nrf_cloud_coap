#include <zephyr/kernel.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>
#include <net/nrf_provisioning.h>
#include <zephyr/sys/reboot.h>

#include "nrf_provisioning_at.h"

#include "cloud.h"

#define INTERVAL 2
#define SEND_PERIOD K_MINUTES(INTERVAL)

K_SEM_DEFINE(lte_connected, 0, 1);

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

		printk("Network registration status: %s\n",
		       evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
		       "Connected - home network" : "Connected - roaming");
		k_sem_give(&lte_connected);
		break;
	case LTE_LC_EVT_RRC_UPDATE:
		printk("RRC mode: %s\n",
		       evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
		       "Connected" : "Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		printk("LTE cell changed: Cell ID: %d, Tracking area: %d\n",
		       evt->cell.id, evt->cell.tac);
		break;
	case LTE_LC_EVT_MODEM_EVENT:
		printk("Modem event: %d\n", evt->modem_evt);
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
		printk("Failed to read modem functional mode\n");
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
			printk("lte_lc_connect() failed %d\n", ret);
		}
		printk("Modem connection restored\n");

		printk("Waiting for modem to acquire network time...\n");

		do {
			k_sleep(K_SECONDS(3));
			ret = nrf_provisioning_at_time_get(time_buf, sizeof(time_buf));
		} while (ret != 0);

		printk("Network time obtained\n");
		ret = fmode;
	} else {
		ret = lte_lc_func_mode_set(new_mode);
		if (ret == 0) {
			printk("Modem set to requested state %d\n", new_mode);
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
		printk("Unable to set modem offline, error %d\n", ret);
	}

	//k_sleep(K_MINUTES(1));

	sys_reboot(SYS_REBOOT_WARM);
}

static void device_mode_cb(enum nrf_provisioning_event event, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (event) {
	case NRF_PROVISIONING_EVENT_START:
		printk("Provisioning started\n");
		break;
	case NRF_PROVISIONING_EVENT_STOP:
		printk("Provisioning stopped\n");
		break;
	case NRF_PROVISIONING_EVENT_DONE:
		printk("Provisioning done, rebooting...\n");
		reboot_device();
		break;
	default:
		printk("Unknown event\n");
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

    printk("nRF Cloud CoAP and MQTT comparison started\n");
    printk("Current mode: %s\n", MODE);
    printk("Message interval: %d minutes\n", INTERVAL);

    err = nrf_modem_lib_init();
    if (err) {
        printk("Modem initialization failed: %d\n", err);
        return 1;
    }

    err = lte_lc_connect_async(lte_handler);
    if (err) {
        printk("Failed to start network connection: %d\n", err);
        return 1;
    }

    k_sem_take(&lte_connected, K_FOREVER);

	err = nrf_provisioning_init(&mmode, &dmode);
	if (err) {
		printk("Failed to initialize provisioning client\n");
	}

    err = cloud_init();
    if (err) {
        printk("Failed to init cloud module: %d\n", err);
        return 1;
    }

    while (1) {
        printk("Sending message\n");
        err = cloud_send((uint8_t *)MESSAGE, sizeof(MESSAGE));
        if (err) {
            printk("Failed to send message: %d\n", err);
            return 1;
        }
        k_sleep(SEND_PERIOD);
    }

    return 0;
}
