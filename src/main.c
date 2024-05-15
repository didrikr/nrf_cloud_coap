#include <zephyr/kernel.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

#include "cloud.h"

#define INTERVAL 57
#define SEND_PERIOD K_MINUTES(INTERVAL)

K_SEM_DEFINE(lte_connected, 0, 1);

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

int main(void)
{
    int err;

    printk("nRF Cloud CoAP and MQTT comparison started\n");
    printk("Current mode: %s\n", MODE);
    printk("Message interval: %d minutes\n", INTERVAL);

    err = nrf_modem_lib_init();
    if (err) {
        printk("Modem initialization failed: %d\n", err);
        return 1;
    }

    err = lte_lc_init_and_connect_async(lte_handler);
    if (err) {
        printk("Failed to start network connection: %d\n", err);
        return 1;
    }

    k_sem_take(&lte_connected, K_FOREVER);

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
