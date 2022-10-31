
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
#include "data.h"

#define LOG_LEVEL CONFIG_DEFAULT_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(temp_main);

#define CMD_GET_UP_TIME 1
#define CMD_READ 2
#define CMD_WRITE 3
#define CMD_KEEP 4

#define STATUS_OK 0
#define STATUS_UNKNOWN_CMD 1
#define STATUS_OUT_OF_BOUNDS 2


Config config;
State state;


static struct k_work on_packet_work;


void ble_packet() {
	k_work_submit(&on_packet_work);
}

#define PACKET_HEADER_SIZE 4

typedef struct {
	uint8_t cmd;
	uint8_t tag;
	uint16_t id;
	union
	{
		struct {
			uint16_t offset;
			uint16_t size;
		} read;
		struct {
			uint16_t offset;
			uint8_t buffer[0];
		} write;
	};
} Request;

typedef struct {
	uint8_t cmd;
	uint8_t status;
	uint16_t id;
	union
	{
		struct {
			uint32_t time;
		} get_up_time;
		struct {
			uint8_t buffer[0];
		} read;
	};
} Response;


static void on_packet_work_handler(struct k_work *work)
{
	const Request* req = (const Request*)&ble_request[0];
	Response* res = (Response*)&ble_response[0];
	void* response_end = &ble_response[PACKET_HEADER_SIZE];
	res->cmd = req->cmd;
	res->status = STATUS_OK;
	res->id = req->id;
	switch (req->cmd)
	{
	case CMD_GET_UP_TIME:
		res->get_up_time.time = k_uptime_get() / (int64_t)1000;
		response_end = &res->get_up_time.time + 1;
		break;
	case CMD_READ: {
		printk("READ %d from %d\n", req->read.size, req->read.offset);
		uint8_t* src = req->tag == 0 ? (uint8_t*)&config : (uint8_t*)&state;
		size_t src_size = req->tag == 0 ? sizeof(config) : sizeof(state);
		if ((size_t)req->read.size > sizeof(ble_response) - PACKET_HEADER_SIZE
			|| (size_t)req->read.offset + (size_t)req->read.size > src_size
			|| req->tag > 1) {
			res->status = STATUS_OUT_OF_BOUNDS;
			break;
		}
		memcpy(res->read.buffer, src + (size_t)req->read.offset, req->read.size);
		response_end = &res->read.buffer[req->read.size];
		break;
	}
	case CMD_WRITE: {
		uint8_t* dst = req->tag == 0 ? (uint8_t*)&config : (uint8_t*)&state;
		size_t dst_size = req->tag == 0 ? sizeof(config) : sizeof(state);
		size_t write_size = ble_request_size - offsetof(Request, write.buffer);
		printk("WRITE %d to %d\n", write_size, req->write.offset);
		if (write_size + (size_t)req->write.offset > sizeof(dst_size) || req->tag > 1) {
			res->status = STATUS_OUT_OF_BOUNDS;
			break;
		}
		memcpy(dst + (size_t)req->write.offset, req->write.buffer, write_size);
		break;
	}
	case CMD_KEEP:
		printk("TODO: save config!\n");
		break;
	default:
		res->status = STATUS_UNKNOWN_CMD;
		break;
	}

	ble_response_size = (uint8_t*)response_end - &ble_response[0]; // TODO: atomic response_size
	printk("Response size %d\n", ble_response_size);
}

static int t = 0;
static int v = 0;
static volatile bool is_new = false;

void packet_received(OutputPacket* packet)
{
	t = packet->temp;
	v = packet->voltage;
	is_new = true;
}

static const char* day_names[7] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

void main(void)
{
	static DateTime dt;

	printk("START\n");

	leds_init();

	k_work_init(&on_packet_work, on_packet_work_handler);

	ble_init();

	multiproto_init();

	while (1) {
		k_msleep(1000);
		if (state.time_shift == 0) {
			int sec = (int)(k_uptime_get() / 1000LL);
			int min = sec / 60; sec %= 60;
			int hour = min / 60; min %= 60;
			int day = hour / 24; hour %= 24;
			printk("%d %02d:%02d:%02d %d\n", day, hour, min, sec, state.time_shift);
		} else {
			uint64_t time = k_uptime_get() / 1000LL + (uint64_t)state.time_shift;
			convert_time_tz(time, &dt, &config.time_zone);
			printk("%d-%02d-%02d %s %02d:%02d:%02d\n", dt.year, dt.month, dt.day, day_names[dt.day_of_week],
				dt.hour, dt.min, dt.sec);
		}
		if (is_new) {
			is_new = false;
			printk("Temperature: %d.%d%d*C\n", t / 100, (t / 10) % 10, t % 10);
			printk("Voltage: %d.%d%dV\n", v / 100, (v / 10) % 10, v % 10);
		}
	}
}


#ifdef TEMP23409304

void main_control()
{
	AWAIT_BEGIN();

	if (t < t_min) {
		heat_on();
		goto on_state;
	} else {
		heat_off();
	}

	while (true) {

		do {
			AWAIT;
			if (t >= t_min) return;
			set_timeout(17);
			AWAIT;
			if (timeout) break;
			if (t < t_min) return;
		} while (true);

		heat_on();

		set_timeout(30);
		AWAIT;
		if (!timeout) return;

		wather_on();

		set_timeout(2);
		AWAIT;
		if (!timeout) return;

		wather_off();

		set_timeout(15);
		AWAIT;
		if (!timeout) return;

on_state:
		do {
			AWAIT;
			if (t <= t_max) return;
			set_timeout(12);
			AWAIT;
			if (timeout) break;
			if (t > t_max) return;
		} while (true);

		heat_off();

	}

}

#endif
