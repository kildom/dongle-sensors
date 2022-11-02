
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

#define NO_CHANNEL  0xFF
#define TEMPERATURE_NO_VALUE 0x7FFF
#define VOLTAGE_NO_VALUE 0x7FFF


static const char* day_names[7] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};


static void packets_queue_handler(struct k_work *work);


K_MSGQ_DEFINE(packets_queue, sizeof(OutputPacket), 8, 4);
K_WORK_DEFINE(packets_queue_work, packets_queue_handler);


void packet_received(OutputPacket* packet)
{
	int err = k_msgq_put(&packets_queue, packet, K_NO_WAIT);
	if (err != 0) {
		LOG_WRN("Packet dropped - queue overrun!");
	} else {
		k_work_submit(&packets_queue_work);
	}
}

int get_up_time() {
	return (int)(k_uptime_get() / 1000LL);
}

static int16_t min_of_channel(int channel_index) {
	int16_t result = 0x7FFF;
	for (int i = 0; i < data_config.node_count; i++) {
		ConfigNode* config_node = &data_config.nodes[i];
		StateNode* state_node = &data_state.nodes[i];
		if (config_node->channel != channel_index) {
			continue;
		}
		if (state_node->temperature == TEMPERATURE_NO_VALUE) {
			return TEMPERATURE_NO_VALUE;
		}
		if (state_node->temperature < result) {
			result = state_node->temperature;
		}
	}
	if (result == 0x7FFF) {
		return TEMPERATURE_NO_VALUE;
	}
	return result;
}

static int16_t max_of_channel(int channel_index) {
	int16_t result = -0x7FFF;
	for (int i = 0; i < data_config.node_count; i++) {
		ConfigNode* config_node = &data_config.nodes[i];
		StateNode* state_node = &data_state.nodes[i];
		if (config_node->channel != channel_index) {
			continue;
		}
		if (state_node->temperature == TEMPERATURE_NO_VALUE) {
			return TEMPERATURE_NO_VALUE;
		}
		if (state_node->temperature > result) {
			result = state_node->temperature;
		}
	}
	if (result == -0x7FFF) {
		return TEMPERATURE_NO_VALUE;
	}
	return result;
}

static int16_t avg_of_channel(int channel_index) {
	int32_t result = 0;
	int32_t count = 0;
	for (int i = 0; i < data_config.node_count; i++) {
		ConfigNode* config_node = &data_config.nodes[i];
		StateNode* state_node = &data_state.nodes[i];
		if (config_node->channel != channel_index) {
			continue;
		}
		if (state_node->temperature == TEMPERATURE_NO_VALUE) {
			return TEMPERATURE_NO_VALUE;
		}
		result += state_node->temperature;
		count++;
	}
	if (count == 0) {
		return TEMPERATURE_NO_VALUE;
	}
	return (result + (count / 2)) / count;
}

static void packet_received_on_work(OutputPacket* packet)
{
	if (data_state.time_shift == 0) {
		int sec = (int)(k_uptime_get() / 1000LL);
		int min = sec / 60; sec %= 60;
		int hour = min / 60; min %= 60;
		int day = hour / 24; hour %= 24;
		printk("%d %02d:%02d:%02d %d\n", day, hour, min, sec, data_state.time_shift);
	} else {
		static DateTime dt;
		uint64_t time = k_uptime_get() / 1000LL + (uint64_t)data_state.time_shift;
		convert_time_tz(time, &dt, &data_config.time_zone);
		printk("%d-%02d-%02d %s %02d:%02d:%02d\n", dt.year, dt.month, dt.day, day_names[dt.day_of_week],
			dt.hour, dt.min, dt.sec);
	}
	int t = packet->temp;
	int v = packet->voltage;
	printk("Node: %04X%08X\n", packet->address_high, packet->address_low);
	printk("Temperature: %d.%d%d*C\n", t / 100, (t / 10) % 10, t % 10);
	printk("Voltage: %d.%d%dV\n", v / 100, (v / 10) % 10, v % 10);

	ConfigNode* config_node = NULL;
	StateNode* state_node = NULL;
	int node_index = -1;

	for (int i = 0; i < data_config.node_count; i++)
	{
		ConfigNode* n = &data_config.nodes[i];
		if (n->addr_high == packet->address_high && n->addr_low == packet->address_low) {
			node_index = i;
			config_node = n;
			state_node = &data_state.nodes[i];
			break;
		}
	}

	if (config_node == NULL) {
		if (data_config.node_count < ARRAY_SIZE(data_config.nodes)) {
			node_index = data_config.node_count;
			config_node = &data_config.nodes[node_index];
			state_node = &data_state.nodes[node_index];
			config_node->addr_high = packet->address_high;
			config_node->addr_low = packet->address_low;
			config_node->channel = NO_CHANNEL;
			strcpy(config_node->name, "[no name]");
			state_node->last_update = 0;
			state_node->temperature = TEMPERATURE_NO_VALUE;
			state_node->voltage = VOLTAGE_NO_VALUE;
			printk("New node added %04X%08X at %d\n", config_node->addr_high, config_node->addr_low, node_index);
			data_config.node_count++;
		} else {
			LOG_ERR("Too many nodes!");
			printk("Too many nodes!\n");
			return;
		}
	}

	printk("Node %d %s\n", node_index, config_node->name);

	state_node->last_update = get_up_time();
	state_node->temperature = packet->temp;
	state_node->voltage = packet->voltage;

	if (config_node->channel < ARRAY_SIZE(data_config.channels)) {
		ConfigChannel *config_channel = &data_config.channels[config_node->channel];
		StateChannel *state_channel = &data_state.channels[config_node->channel];
		switch (config_channel->func)
		{
		case FUNC_MIN:
			state_channel->temperature = min_of_channel(config_node->channel);
			break;
		case FUNC_MAX:
			state_channel->temperature = max_of_channel(config_node->channel);
			break;
		case FUNC_AVG:
			state_channel->temperature = avg_of_channel(config_node->channel);
			break;
		default:
			break;
		}
		printk("Channel %d %s temperature: %d.%d%d*C\n", config_node->channel, config_channel->name, t / 100, (t / 10) % 10, t % 10);
	}

}

static void packets_queue_handler(struct k_work *work)
{
	int err;
	OutputPacket packet;
	do {
		err = k_msgq_get(&packets_queue, &packet, K_NO_WAIT);
		if (err == 0) {
			packet_received_on_work(&packet);
		}
	} while (err == 0);
}

void main(void)
{
	printk("START\n");

	data_init();

	leds_init();

	ble_init();

	multiproto_init();
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
