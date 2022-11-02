
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
LOG_MODULE_REGISTER(temp_blecmd);


#define CMD_GET_UP_TIME 1
#define CMD_READ 2
#define CMD_WRITE 3
#define CMD_KEEP 4

#define STATUS_OK 0
#define STATUS_UNKNOWN_CMD 1
#define STATUS_OUT_OF_BOUNDS 2

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
	size_t req_size;
	size_t res_max_size;
	const Request* req = (const Request*)ble_request_get(&req_size);
	Response* res = (Response*)ble_response_prepare(&res_max_size);
	void* response_end = (uint8_t*)res + PACKET_HEADER_SIZE;
	if (req == NULL || res == NULL) {
		return;
	}
	uint8_t* mem = req->tag == 0 ? (uint8_t*)&data_config : (uint8_t*)&data_state;
	size_t mem_size = req->tag == 0 ? sizeof(data_config) : sizeof(data_state);
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
		LOG_DBG("READ %d from %d\n", req->read.size, req->read.offset);
		if ((size_t)req->read.size > res_max_size - PACKET_HEADER_SIZE
			|| (size_t)req->read.offset + (size_t)req->read.size > mem_size
			|| req->tag > 1) {
			res->status = STATUS_OUT_OF_BOUNDS;
			break;
		}
		memcpy(res->read.buffer, mem + (size_t)req->read.offset, req->read.size);
		response_end = &res->read.buffer[req->read.size];
		break;
	}
	case CMD_WRITE: {
		size_t write_size = req_size - offsetof(Request, write.buffer);
		LOG_DBG("WRITE %d to %d\n", write_size, req->write.offset);
		if (write_size + (size_t)req->write.offset > mem_size || req->tag > 1) {
			res->status = STATUS_OUT_OF_BOUNDS;
			break;
		}
		memcpy(mem + (size_t)req->write.offset, req->write.buffer, write_size);
		break;
	}
	case CMD_KEEP:
		LOG_ERR("TODO: save config!\n");
		break;
	default:
		res->status = STATUS_UNKNOWN_CMD;
		break;
	}

	ble_response_send((uint8_t*)response_end - (uint8_t*)res);
}


K_WORK_DEFINE(on_packet_work, on_packet_work_handler);


void ble_packet() {
	k_work_submit(&on_packet_work);
}
