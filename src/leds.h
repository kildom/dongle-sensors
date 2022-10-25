#ifndef _leds_h_
#define _leds_h_

static inline void led_off(int n) {
	NRF_P0->OUTSET = (1 << (13 + n));
}

static inline void led_on(int n) {
	NRF_P0->OUTCLR = (1 << (13 + n));
}

static inline void led_toggle(int n) {
	uint32_t mask = (1 << (13 + n));
	if (NRF_P0->OUT & mask) {
		NRF_P0->OUTCLR = mask;
	} else {
		NRF_P0->OUTSET = mask;
	}
}

static inline void led_set(int n, int value) {
	uint32_t mask = (1 << (13 + n));
	if (value) {
		NRF_P0->OUTCLR = mask;
	} else {
		NRF_P0->OUTSET = mask;
	}
}

static inline void leds_init() {
	NRF_P0->PIN_CNF[13] = 1;
	NRF_P0->PIN_CNF[14] = 1;
	NRF_P0->PIN_CNF[15] = 1;
	NRF_P0->PIN_CNF[16] = 1;
	led_off(0);
	led_off(1);
	led_off(2);
	led_off(3);
}

#endif
