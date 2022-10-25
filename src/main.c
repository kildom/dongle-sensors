/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zephyr.h>
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

#include "multiproto.h"
#include "leds.h"

#define LOG_LEVEL CONFIG_DEFAULT_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(temp_main);

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

#define CHUNK_SIZE 16
#define CHUNK_FLAG_BEGIN 0x40
#define CHUNK_FLAG_END 0x80
#define CHUNK_ID_MASK 0x3F

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


BT_GATT_SERVICE_DEFINE(vnd_svc,
	BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
	BT_GATT_CHARACTERISTIC(&vnd_ept_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_vnd, write_vnd, NULL),
);


void main(void)
{
	int ret;

	printk("START\n");

	leds_init();

	ret = bt_enable(NULL);
	if (ret) {
		printk("Bluetooth init failed (err %d)\n", ret);
		k_oops();
	}

	ret = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
		BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL), ad, ARRAY_SIZE(ad), NULL, 0);
	if (ret) {
		printk("Advertising failed to start (err %d)\n", ret);
		return;
	}

	multiproto_init();

	while (1) {
		k_msleep(3000);
		printk("Tick\n");
	}
}

__attribute__((aligned(4)))
static uint8_t packet[256];

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	int16_t temp;
	int16_t voltage;
}__attribute__((aligned(4)))  OutputPacket;

typedef struct {
	uint32_t address_low;
	uint16_t address_high;
	uint16_t _reserved;
	uint16_t flags;
}__attribute__((aligned(4))) InputPacket;

static OutputPacket *const output_packet = (OutputPacket*)&packet[0];
static InputPacket *const input_packet = (InputPacket*)&packet[0];

static const uint32_t FREQUENCY = 2400;
static const uint32_t BASE_ADDR = 0x63e0;
static const uint32_t PREFIX_BYTE_ADDR = 0x17;
static const uint32_t CRC_POLY = 0x864CFB; // CRC-24-Radix-64 (OpenPGP)
static const uint16_t INPUT_FLAG_ACK = 0x8000;

K_MSGQ_DEFINE(my_msgq, sizeof(OutputPacket), 10, 4);

bool multiproto_radio_callback2()
{
	if (NRF_RADIO->EVENTS_DISABLED) {
		NRF_RADIO->EVENTS_DISABLED = 0;
		NRF_RADIO->INTENCLR = 0xFFFFFFFF;
		NRF_RADIO->POWER = 0;
		return false;
	} else if (NRF_RADIO->EVENTS_END) {
		NRF_RADIO->EVENTS_END = 0;
		led_toggle(1);
		if ((NRF_RADIO->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) != RADIO_CRCSTATUS_CRCSTATUS_CRCOk ||
			(NRF_RADIO->RXMATCH & RADIO_RXMATCH_RXMATCH_Msk) != 0)
		{
			NRF_RADIO->TASKS_START = 1;
			return true;
		}
		led_toggle(0);
	}
	return true;
}

void multiproto_start_callback2()
{
	NRF_RADIO->POWER = 1;
	NRF_RADIO->INTENCLR = 0xFFFFFFFF;
	NVIC_EnableIRQ(RADIO_IRQn);
	NVIC_SetPriority(RADIO_IRQn, 0);
	NRF_RADIO->PACKETPTR = (uint32_t)&packet[0];
	NRF_RADIO->FREQUENCY = FREQUENCY - 2400;
	NRF_RADIO->MODE = 2;
	NRF_RADIO->PCNF0 =
		(0 << RADIO_PCNF0_LFLEN_Pos) |
		(0 << RADIO_PCNF0_S0LEN_Pos) |
		(0 << RADIO_PCNF0_S1LEN_Pos);
	NRF_RADIO->PCNF1 =
		(10 << RADIO_PCNF1_MAXLEN_Pos) |
		(10 << RADIO_PCNF1_STATLEN_Pos) |
		(2 << RADIO_PCNF1_BALEN_Pos) |
		(RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos);
	NRF_RADIO->BASE0 = BASE_ADDR;
	NRF_RADIO->PREFIX0 = PREFIX_BYTE_ADDR << RADIO_PREFIX0_AP0_Pos;
	NRF_RADIO->TXADDRESS = 0;
	NRF_RADIO->RXADDRESSES = RADIO_RXADDRESSES_ADDR0_Msk;
	NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos;
	NRF_RADIO->CRCPOLY = CRC_POLY;
	NRF_RADIO->CRCINIT = 0;

	NRF_RADIO->EVENTS_END = 0;
	NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
	NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;
	NRF_RADIO->TASKS_RXEN = 1;
}

bool multiproto_end_callback2()
{
	NRF_RADIO->SHORTS = 0;
	NRF_RADIO->INTENCLR = 0xFFFFFFFF;
	NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
	NRF_RADIO->TASKS_DISABLE = 1;
	return true;
}

typedef enum {
	_RECV_EV_RESERVED,
	RECV_EV_RADIO,
	RECV_EV_START,
	RECV_EV_END,
} RecvEventType;

#define _AWAIT2(l, c) \
	_resume_point = &&_resume_label_##l##_##c; \
	ev = _RECV_EV_RESERVED; \
	_resume_label_##l##_##c: \
	do { } while (0)
#define _AWAIT1(l, c) _AWAIT2(l, c)
#define AWAIT _AWAIT1(__LINE__, __COUNTER__)

#define ASYNC_BEGIN \
	static void* _resume_point = &&_resume_label_start; \
	goto *_resume_point; \
	_resume_label_start: \
	do { } while (0)


bool receive_async_func(RecvEventType ev) {

	LOG_WRN("receive_async_func %d %d", ev, NRF_RADIO->STATE);

	if (ev == RECV_EV_START) {
		goto start_from_beginning;
	}
	ASYNC_BEGIN;
	start_from_beginning:

	// Init minimal
	NRF_RADIO->POWER = 1;
	NRF_RADIO->INTENCLR = 0xFFFFFFFF;
	NVIC_EnableIRQ(RADIO_IRQn);
	NVIC_SetPriority(RADIO_IRQn, 0);

	// If other protocol left radio enabled, disable it
	if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
		NRF_RADIO->EVENTS_DISABLED = 0;
		NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
		NRF_RADIO->SHORTS = 0;
		NRF_RADIO->TASKS_DISABLE = 1;
		if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
			AWAIT;
			if (ev == RECV_EV_END) goto end_during_disabling;
			if (!NRF_RADIO->EVENTS_DISABLED) return true;
			NRF_RADIO->EVENTS_DISABLED = 0;
		}
	}

	// Continue radio initialization
	NRF_RADIO->PACKETPTR = (uint32_t)&packet[0];
	NRF_RADIO->FREQUENCY = FREQUENCY - 2400;
	NRF_RADIO->MODE = 2;
	NRF_RADIO->PCNF0 =
		(0 << RADIO_PCNF0_LFLEN_Pos) |
		(0 << RADIO_PCNF0_S0LEN_Pos) |
		(0 << RADIO_PCNF0_S1LEN_Pos);
	NRF_RADIO->PCNF1 =
		(10 << RADIO_PCNF1_MAXLEN_Pos) |
		(10 << RADIO_PCNF1_STATLEN_Pos) |
		(2 << RADIO_PCNF1_BALEN_Pos) |
		(RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos);
	NRF_RADIO->BASE0 = BASE_ADDR;
	NRF_RADIO->PREFIX0 = PREFIX_BYTE_ADDR << RADIO_PREFIX0_AP0_Pos;
	NRF_RADIO->TXADDRESS = 0;
	NRF_RADIO->RXADDRESSES = RADIO_RXADDRESSES_ADDR0_Msk;
	NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos;
	NRF_RADIO->CRCPOLY = CRC_POLY;
	NRF_RADIO->CRCINIT = 0;
	NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos4dBm;

	// Loop over single cycle: RX and TX pair
	while (1) {
		// Enable RX and (with short) start receiving packets
		NRF_RADIO->EVENTS_END = 0;
		NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;
		NRF_RADIO->INTENCLR = 0xFFFFFFFF;
		NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
		NRF_RADIO->TASKS_RXEN = 1;
		// Loop over packets receiving until we get valid packet
		while (1) {
			// Wait for RX packet end (or timeslot "END" event)
			AWAIT;
			LOG_WRN("resumed %d %d", ev, NRF_RADIO->STATE);
			if (ev == RECV_EV_END) goto end_at_rx;
			if (!NRF_RADIO->EVENTS_END) return true;
			NRF_RADIO->EVENTS_END = 0;
			led_toggle(0);
			// Check if packet is valid
			if ((NRF_RADIO->CRCSTATUS & RADIO_CRCSTATUS_CRCSTATUS_Msk) != (RADIO_CRCSTATUS_CRCSTATUS_CRCOk << RADIO_CRCSTATUS_CRCSTATUS_Pos) ||
				(NRF_RADIO->RXMATCH & RADIO_RXMATCH_RXMATCH_Msk) != 0)
			{
				// Resume receiving if invalid
				NRF_RADIO->TASKS_START = 1;
				continue;
			} else {
				// Break this loop to process valid packet further
				break;
			}
		}
		led_toggle(1);
		// Disable RX
		NRF_RADIO->SHORTS = 0;
		NRF_RADIO->INTENCLR = RADIO_INTENSET_END_Msk;
		NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
		NRF_RADIO->TASKS_DISABLE = 1;
		// Wait until RX disabled (or timeslot "END" event)
		AWAIT;
		if (ev == RECV_EV_END) goto end_during_disabling;
		if (!NRF_RADIO->EVENTS_DISABLED) return true;
		NRF_RADIO->EVENTS_DISABLED = 0;
		// Process packet
		LOG_ERR("Packet from %04X%08X", output_packet->address_high, output_packet->address_low);
		int t = output_packet->temp;
		LOG_ERR("Temperature: %d.%d%d*C", t / 100, (t / 10) % 10, t % 10);
		int v = output_packet->voltage;
		LOG_ERR("Voltage: %d.%d%dV", v / 100, (v / 10) % 10, v % 10);
		input_packet->_reserved = 0;
		input_packet->flags = INPUT_FLAG_ACK;
		__DMB();
		// Enable TX and (with short) send packet and (with short) disable TX
		NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk;
		NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
		NRF_RADIO->TASKS_TXEN = 1;
		// Wait until TX disabled (or timeslot "END" event)
		AWAIT;
		if (ev == RECV_EV_END) goto end_at_tx;
		if (!NRF_RADIO->EVENTS_DISABLED) return true;
		NRF_RADIO->EVENTS_DISABLED = 0;
	}

end_at_tx:
	if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
		NRF_RADIO->EVENTS_DISABLED = 0;
		NRF_RADIO->SHORTS = 0;
		NRF_RADIO->TASKS_DISABLE = 1;
		if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
			goto end_during_disabling;
		}
	}
	goto power_off;
end_at_rx:
	LOG_WRN("end at rx %d %d %d", ev, NRF_RADIO->STATE, NRF_RADIO->EVENTS_DISABLED);
	NRF_RADIO->EVENTS_DISABLED = 0;
	NRF_RADIO->SHORTS = 0;
	NRF_RADIO->INTENCLR = RADIO_INTENSET_END_Msk;
	NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
	NRF_RADIO->TASKS_DISABLE = 1;
end_during_disabling:
	LOG_WRN("wait for disabled %d %d", NRF_RADIO->STATE, NRF_RADIO->EVENTS_DISABLED);
	_resume_point = &&_resume_label_aaaa;
	return true;
	_resume_label_aaaa:
	LOG_WRN("resume state %d %d", NRF_RADIO->STATE, NRF_RADIO->EVENTS_DISABLED);
	if (!NRF_RADIO->EVENTS_DISABLED) return true;
	NRF_RADIO->EVENTS_DISABLED = 0;
power_off:
	NRF_RADIO->POWER = 0;
	LOG_WRN("powered off, ret false");
	return false;
}



bool multiproto_radio_callback()
{
	return receive_async_func(RECV_EV_RADIO);
}

bool multiproto_start_callback()
{
	LOG_ERR("%d", (int)(k_uptime_get() / 1000LL));
	return receive_async_func(RECV_EV_START);
}

bool multiproto_end_callback()
{
	return receive_async_func(RECV_EV_END);
}

static const uint32_t DAYS_PER_LEAP_YEAR = 366;
static const uint32_t DAYS_PER_COMMON_YEAR = 365;
static const uint32_t DAYS_PER_COMMON_4_YEARS = 4 * DAYS_PER_COMMON_YEAR + 1;
static const uint32_t DAYS_PER_COMMON_100_YEARS = 25 * DAYS_PER_COMMON_4_YEARS - 1;
static const uint32_t DAYS_PER_400_YEARS = 4 * DAYS_PER_COMMON_100_YEARS + 1;
static const uint32_t DAYS_BEFORE_1970 = 719528;

/*

const DAYS_PER_LEAP_YEAR = 366;
const DAYS_PER_COMMON_YEAR = 365;
const DAYS_PER_COMMON_4_YEARS = 4 * DAYS_PER_COMMON_YEAR + 1;
const DAYS_PER_COMMON_100_YEARS = 25 * DAYS_PER_COMMON_4_YEARS - 1;
const DAYS_PER_400_YEARS = 4 * DAYS_PER_COMMON_100_YEARS + 1;
const DAYS_BEFORE_1970 = 719528;

400: P--------------------- P--------------------- P--------------------- P----------...
100: N---- N---- N---- N--- N---- N---- N---- N--- N---- N---- N---- N--- N---- N---....
  4: P-P-P P-P-P ...

c400 = day_from_0 / DAYS_PER_400_YEARS
d400 = day_from_0 % DAYS_PER_400_YEARS
if (d400 < DAYS_PER_LEAP_YEAR) {
	y = 400 * c400;
	leap = true;
	day_of_year = d400;
	return;
}
d400 = d400 - 1
c100 = d400 / DAYS_PER_COMMON_100_YEARS
d100 = d400 % DAYS_PER_COMMON_100_YEARS
if (d100 < DAYS_PER_COMMON_YEAR) {
	y = 400 * c400 + 100 * c100;
	leap = false;
	day_of_year = d100;
	return;
}
d100 = d100 + 1
c4 = d100 / DAYS_PER_COMMON_4_YEARS
d4 = d100 % DAYS_PER_COMMON_4_YEARS
if (d4 < DAYS_PER_LEAP_YEAR) {
	y = 400 * c400 + 100 * c100 + 4 * c4;
	leap = true;
	day_of_year = d4;
	return;
}
d4 = d4 - 1
c1 = d4 / DAYS_PER_COMMON_YEAR
d1 = d4 % DAYS_PER_COMMON_YEAR
y = 400 * c400 + 100 * c100 + 4 * c4 + c1;
leap = false;
day_of_year = d1;
return;

if (leap) {
	month = day_of_year * 2 / 61
	day = day_of_year - days_before_month_leap[month]; // 13-element array
} else {
	month = (day_of_year * 100 + 214) / 3052;
	day = day_of_year - days_before_month_common[month]; // 13-element array
}
if (day < 0) {
	month -= 1
	day = 31 + day
}

day += 1
month += 1

day_of_weak = (day_from_0 + X) % 7;


*/
