
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <mpsl/mpsl_work.h>
#include <mpsl_timeslot.h>
#include <mpsl.h>
#include <hal/nrf_timer.h>

#include "calendar.h"
#include "multiproto.h"
#include "leds.h"
#include "radio.h"
#include "ble.h"

#define LOG_LEVEL CONFIG_DEFAULT_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(temp_ble);

#define CHUNK_SIZE 16
#define CHUNK_FLAG_BEGIN 0x40
#define CHUNK_FLAG_END 0x80
#define CHUNK_ID_MASK 0x3F

#define BT_UUID_CUSTOM_SERVICE_VAL BT_UUID_128_ENCODE(0xCC2AF14A, 0x2AAF, 0x4C6E, 0xB2E4, 0x3856EE2B4267)

static struct bt_uuid_128 vnd_uuid = BT_UUID_INIT_128(BT_UUID_CUSTOM_SERVICE_VAL);

static struct bt_uuid_128 vnd_ept_uuid = BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x45CC8E0B, 0x8507, 0x45F7, 0xAC95, 0xB798D0FD732A));

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CUSTOM_SERVICE_VAL),
};


uint8_t ble_request[REQUEST_SIZE];
int ble_request_size = 0;
static uint8_t request_id = 255;
static int request_offset = 0;


uint8_t ble_response[RESPONSE_SIZE];
int ble_response_size = 0;
static uint8_t response_id = 255;
static int response_offset = 0;


static ssize_t read_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	uint8_t temp[1 + CHUNK_SIZE];
	LOG_DBG("GATT read %d bytes at %d", len, offset);

	if (offset != 0 || len <= 1 + CHUNK_SIZE) {
		LOG_DBG("GATT read INVALID");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	temp[0] = response_id;

	if (ble_response_size == 0) {
		return bt_gatt_attr_read(conn, attr, buf, len, offset, temp, 0);
	} else if (response_offset >= ble_response_size) {
		temp[0] |= CHUNK_FLAG_END;
		return bt_gatt_attr_read(conn, attr, buf, len, offset, temp, 1);
	}

	int send_size = ble_response_size - response_offset;
	if (send_size > CHUNK_SIZE) {
		send_size = CHUNK_SIZE;
	}

	if (response_offset == 0) {
		temp[0] |= CHUNK_FLAG_BEGIN;
	}
	memcpy(&temp[1], &ble_response[response_offset], send_size);
	response_offset += send_size;
	if (response_offset == ble_response_size) {
		temp[0] |= CHUNK_FLAG_END;
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, temp, 1 + send_size);
}


static ssize_t write_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags)
{
	LOG_DBG("GATT write %d bytes at %d", len, offset);

	int err = 0;
	uint8_t* input = (uint8_t*)buf + 1;
	int input_len = len - 1;
	uint8_t input_flags = input[-1];

	if (offset != 0) {
		err = BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
		goto error;
	} else if (input_len > CHUNK_SIZE || input_len < 0) {
		err = BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		goto error;
	} else if (input_flags & CHUNK_FLAG_BEGIN) {
		request_id = input_flags & CHUNK_ID_MASK;
		request_offset = 0;
		ble_request_size = 0;
	} else if ((input_flags & CHUNK_ID_MASK) != request_id) {
		err = BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		goto error;
	}

	if (request_offset + input_len > sizeof(ble_request)) {
		err = BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		goto error;
	}

	memcpy(&ble_request[request_offset], input, input_len);
	request_offset += input_len;
	ble_request_size = request_offset;

	if (input_flags & CHUNK_FLAG_END) {
		response_id = request_id;
		ble_response_size = 0;
		response_offset = 0;
		request_id = 255;
		request_offset = 0;
		ble_packet();
	}

	return len;

error:
	request_id = 255;
	request_offset = 0;
	ble_request_size = 0;
	return err;
}


BT_GATT_SERVICE_DEFINE(vnd_svc,
	BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
	BT_GATT_CHARACTERISTIC(&vnd_ept_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_vnd, write_vnd, NULL),
);


void ble_init()
{
	int ret;
	ret = bt_enable(NULL);
	if (ret) {
		printk("Bluetooth init failed (err %d)\n", ret);
		k_oops();
	}

	ret = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
		BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL), ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret) {
		printk("Advertising failed to start (err %d)\n", ret);
		k_oops();
	}
}
