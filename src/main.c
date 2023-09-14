#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

#include <bluetooth/services/nus.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/settings/settings.h>

#include <stdio.h>

#include <zephyr/logging/log.h>

#define LOG_MODULE_NAME peripheral_uart
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define STACKSIZE CONFIG_BT_NUS_THREAD_STACK_SIZE
#define PRIORITY 7

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED DK_LED1
#define RUN_LED_BLINK_INTERVAL 1000

#define CON_STATUS_LED DK_LED2

static struct bt_conn *current_conn;
static struct bt_conn *auth_conn;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err)
	{
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected %s", addr);

	current_conn = bt_conn_ref(conn);

	dk_set_led_on(CON_STATUS_LED);
	char example_data[] = "Hello, NUS!";
	bt_nus_send(NULL, example_data, sizeof(example_data) - 1);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason %u)", addr, reason);

	if (auth_conn)
	{
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}

	if (current_conn)
	{
		bt_conn_unref(current_conn);
		current_conn = NULL;
		dk_set_led_off(CON_STATUS_LED);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
						  uint16_t len)
{
	char addr[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));

	char received_string[len + 1];
	memcpy(received_string, data, len);
	received_string[len] = '\0';

	LOG_INF("Received data: %s", received_string);
}

static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
};

void error(void)
{
	dk_set_leds_state(DK_ALL_LEDS_MSK, DK_NO_LEDS_MSK);

	while (true)
	{
		/* Spin for ever */
		k_sleep(K_MSEC(1000));
	}
}

static void configure_gpio(void)
{
	int err;

	err = dk_leds_init();
	if (err)
	{
		LOG_ERR("Cannot init LEDs (err: %d)", err);
	}
}

int main(void)
{
	int blink_status = 0;
	int err = 0;

	configure_gpio();

	err = bt_enable(NULL);
	if (err)
	{
		error();
	}

	LOG_INF("Bluetooth initialized");
	settings_load();
	err = bt_nus_init(&nus_cb);
	if (err)
	{
		LOG_ERR("Failed to initialize UART service (err: %d)", err);
		return 0;
	}
	else
	{
		LOG_INF("BLE initialized successfully");
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd,
						  ARRAY_SIZE(sd));
	if (err)
	{
		LOG_ERR("Advertising failed to start (err %d)", err);
		return 0;
	}
	else
	{
		LOG_INF("Advertising started successfully");
	}

	for (;;)
	{
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		const char *message = "hello from nus";
		uint16_t len = strlen(message);

		bt_nus_send(current_conn, (uint8_t *)message, len);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}
