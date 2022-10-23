/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zephyr.h>
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

#define BT_UUID_CUSTOM_SERVICE_VAL \
	BT_UUID_128_ENCODE(0xCC2AF14A, 0x2AAF, 0x4C6E, 0xB2E4, 0x3856EE2B4267)

static struct bt_uuid_128 vnd_uuid = BT_UUID_INIT_128(
	BT_UUID_CUSTOM_SERVICE_VAL);

static struct bt_uuid_128 vnd_ept_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x45CC8E0B, 0x8507, 0x45F7, 0xAC95, 0xB798D0FD732A));


static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CUSTOM_SERVICE_VAL),
};

static void led_set(int n) {
	NRF_P0->OUTSET = (1 << (13 + n));
}

static void led_clear(int n) {
	NRF_P0->OUTCLR = (1 << (13 + n));
}

static void led_toggle(int n) {
	uint32_t mask = (1 << (13 + n));
	if (NRF_P0->OUT & mask) {
		NRF_P0->OUTCLR = mask;
	} else {
		NRF_P0->OUTSET = mask;
	}
}

#define CHUNK_SIZE 16
#define CHUNK_FLAG_BEGIN 0x40
#define CHUNK_FLAG_END 0x80
#define CHUNK_ID_MASK 0x3F

#define VND_MAX_LEN 1000

static uint8_t request[512];
static uint8_t request_id = 255;
static int request_offset = 0;
static int request_size = 0;

static uint8_t response[512];
static uint8_t response_id = 255;
static int response_offset = 0;
static int response_size = 0;

static void on_packet() {
	memcpy(response, request, request_size);
	response_size = request_size;
	printk("Packet %d bytes\n", request_size);
}

static ssize_t read_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	uint8_t temp[1 + CHUNK_SIZE];
	printk("GATT read %d bytes at %d\n", len, offset);

	if (offset != 0 || len <= 1 + CHUNK_SIZE) {
		printk("GATT read INVALID\n");
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (response_size == 0) {
		return bt_gatt_attr_read(conn, attr, buf, len, offset, temp, 0);
	} else if (response_offset >= response_size) {
		temp[0] = CHUNK_FLAG_END;
		return bt_gatt_attr_read(conn, attr, buf, len, offset, temp, 1);
	}

	int send_size = response_size - response_offset;
	if (send_size > CHUNK_SIZE) {
		send_size = CHUNK_SIZE;
	}
	temp[0] = response_id;
	if (response_offset == 0) {
		temp[0] |= CHUNK_FLAG_BEGIN;
	}
	memcpy(&temp[1], &response[response_offset], send_size);
	response_offset += send_size;
	if (response_offset == response_size) {
		temp[0] |= CHUNK_FLAG_END;
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, temp, 1 + send_size);
}

static ssize_t write_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags)
{
	printk("GATT write %d bytes at %d\n", len, offset);

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
		request_size = 0;
	} else if ((input_flags & CHUNK_ID_MASK) != request_id) {
		err = BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		goto error;
	}

	if (request_offset + input_len > sizeof(request)) {
		err = BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		goto error;
	}

	memcpy(&request[request_offset], input, input_len);
	request_offset += input_len;
	request_size = request_offset;

	if (input_flags & CHUNK_FLAG_END) {
		response_id = request_id;
		response_size = 0;
		response_offset = 0;
		request_id = 255;
		request_offset = 0;
		on_packet();
	}

	return len;

error:
	request_id = 255;
	request_offset = 0;
	request_size = 0;
	return err;
}

/* Vendor Primary Service Declaration */
BT_GATT_SERVICE_DEFINE(vnd_svc,
	BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
	BT_GATT_CHARACTERISTIC(&vnd_ept_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_vnd, write_vnd, NULL),
);

struct bt_le_conn_param* conn_param = BT_LE_CONN_PARAM(4 * BT_GAP_INIT_CONN_INT_MIN,
						  4 * BT_GAP_INIT_CONN_INT_MAX,
						  0, 4 * 400);

void setup_conn(struct bt_conn *conn) {
	int ret = bt_conn_le_param_update(conn, conn_param);
	if (ret) {
		printk("bt_conn_le_param_update failed (err %d)\n", ret);
		return;
	}
}


static void connected(struct bt_conn *conn, uint8_t err)
{
	printk("connected %d\n", err);
	//setup_conn(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("disconnected %d\n", reason);
}


static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	printk("le_param_req interval=%d-%d latency=%d timeout=%d\n",
		param->interval_min, param->interval_max, param->latency, param->timeout);
	return param->interval_min >= conn_param->interval_min &&
		param->interval_max >= conn_param->interval_max;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
				 uint16_t latency, uint16_t timeout)
{
	printk("le_param_updated interval=%d latency=%d timeout=%d\n", interval, latency, timeout);
}

struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_req = le_param_req,
	.le_param_updated = le_param_updated,
};

static const uint32_t TIME_SLOT_US = 2 * MPSL_TIMESLOT_EXTENSION_TIME_MIN_US
	+ 4 * (MPSL_TIMESLOT_EXTENSION_MARGIN_MIN_US + MPSL_TIMESLOT_EXTENSION_PROCESSING_TIME_MAX_US);

static const uint32_t TIME_SLOT_MARGIN_US = 2 * (MPSL_TIMESLOT_EXTENSION_MARGIN_MIN_US + MPSL_TIMESLOT_EXTENSION_PROCESSING_TIME_MAX_US);

//#define TIMESLOT_REQUEST_DISTANCE_US (1)
//#define TIMESLOT_LENGTH_US     MPSL_TIMESLOT_LENGTH_MAX_US
//#define TIMER_EXPIRY_US        (TIMESLOT_LENGTH_US - 50)

static bool session_opened = false;
static struct k_work my_work;
static mpsl_timeslot_session_id_t session_id = 0xFFu;
static mpsl_timeslot_signal_return_param_t signal_callback_return_param;

static const uint32_t ADV_TIME_SLOT = 6000;
static const uint32_t ADV_JUMP_TIME = ADV_TIME_SLOT * 4 / 3;

/* Timeslot requests */
static mpsl_timeslot_request_t timeslot_request_earliest = {
	.request_type = MPSL_TIMESLOT_REQ_TYPE_EARLIEST,
	.params.earliest.hfclk = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED,
	.params.earliest.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
	.params.earliest.length_us = TIME_SLOT_US,
	.params.earliest.timeout_us = 1000000
};

static mpsl_timeslot_request_t timeslot_request_normal = {
	.request_type = MPSL_TIMESLOT_REQ_TYPE_NORMAL,
	.params.normal.hfclk = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED,
	.params.normal.priority = MPSL_TIMESLOT_PRIORITY_NORMAL,
	.params.normal.distance_us = 100000,
	.params.normal.length_us = TIME_SLOT_US
};

static enum {
	STATE_IDLE,
	STATE_ACTIVE,
	STATE_EXTENDING,
	STATE_STOPPING,
} mpsl_state = STATE_IDLE;
uint32_t end_time_us = 0;

static mpsl_timeslot_signal_return_param_t *mpsl_timeslot_callback(
	mpsl_timeslot_session_id_t session_id,
	uint32_t signal_type)
{
	(void) session_id; /* unused parameter */
	uint8_t input_data = (uint8_t)signal_type;
	uint32_t input_data_len;

	mpsl_timeslot_signal_return_param_t *p_ret_val = NULL;

	switch (signal_type) {
	case MPSL_TIMESLOT_SIGNAL_START:
		mpsl_state = STATE_ACTIVE;
		end_time_us = TIME_SLOT_US;
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, end_time_us - TIME_SLOT_MARGIN_US);
		nrf_timer_int_enable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		led_set(0);
		break;
	case MPSL_TIMESLOT_SIGNAL_TIMER0:
		mpsl_state = STATE_EXTENDING;
		//gpio_pin_set_dt(&led, 1);
		//nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		nrf_timer_event_clear(NRF_TIMER0, NRF_TIMER_EVENT_COMPARE0);
		//signal_callback_return_param.params.request.p_next = &timeslot_request_normal;
		//signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
		//signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_END;
		//mpsl_work_submit(&my_work);
		signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_EXTEND;
		signal_callback_return_param.params.extend.length_us = TIME_SLOT_US;
		return &signal_callback_return_param;
	case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED:
		mpsl_state = STATE_STOPPING;
		nrf_timer_int_disable(NRF_TIMER0, NRF_TIMER_INT_COMPARE0_MASK);
		led_clear(0);
		// TODO: turn_radio_off()
		//break;
		//case RADIO off done:
		mpsl_state = STATE_IDLE;
		timeslot_request_normal.params.normal.distance_us = end_time_us + ADV_JUMP_TIME;
		signal_callback_return_param.params.request.p_next = &timeslot_request_normal;
		signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
		//mpsl_work_submit(&my_work);
		//signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_END;
		return &signal_callback_return_param;
	case MPSL_TIMESLOT_SIGNAL_EXTEND_SUCCEEDED:
		mpsl_state = STATE_ACTIVE;
		end_time_us += TIME_SLOT_US;
		nrf_timer_cc_set(NRF_TIMER0, NRF_TIMER_CC_CHANNEL0, end_time_us - TIME_SLOT_MARGIN_US);
		break;
	case MPSL_TIMESLOT_SIGNAL_BLOCKED:
		led_toggle(1);
		mpsl_work_submit(&my_work);
		break;
	case MPSL_TIMESLOT_SIGNAL_CANCELLED:
		led_toggle(2);
		//signal_callback_return_param.params.request.p_next = &timeslot_request_normal;
		//signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_REQUEST;
		//p_ret_val = &signal_callback_return_param;
		//return p_ret_val;
		mpsl_work_submit(&my_work);
		break;
	case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
		break;
	case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
		break;
	default:
		printk("unexpected signal: %u\n", signal_type);
		//k_oops();
		break;
	}

	signal_callback_return_param.callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE;
	p_ret_val = &signal_callback_return_param;
	return p_ret_val;
}

static void work_handler(struct k_work *work)
{
	int err;
	if (!session_opened) {
		err = mpsl_timeslot_session_open(mpsl_timeslot_callback, &session_id);
		if (err) {
			printk("Timeslot session open error: %d\n", err);
			k_oops();
		}
		printk("Session open OK\n");
		session_opened = true;
		mpsl_work_submit(&my_work);
		return;
	}
	err = mpsl_timeslot_request(session_id, &timeslot_request_earliest);
	if (err) {
		printk("Timeslot request error: %d\n", err);
		k_oops();
	}
}


void main(void)
{
	int ret;

	NRF_P0->PIN_CNF[13] = 1;
	NRF_P0->PIN_CNF[14] = 1;
	NRF_P0->PIN_CNF[15] = 1;
	NRF_P0->PIN_CNF[16] = 1;
	led_set(0);
	led_set(1);
	led_set(2);
	led_set(3);

	printk("START\n");

	ret = bt_enable(NULL);
	if (ret) {
		printk("Bluetooth init failed (err %d)\n", ret);
		return;
	}

	bt_conn_cb_register(&conn_callbacks);

	ret = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
		BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL), ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret) {
		printk("Advertising failed to start (err %d)\n", ret);
		return;
	}

	k_work_init(&my_work, work_handler);
	mpsl_work_submit(&my_work);

	while (1) {
		k_msleep(3000);
		printk("Tick\n");
	}
}
