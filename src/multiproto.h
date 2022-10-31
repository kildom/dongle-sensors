#ifndef _multiproto_h_
#define _multiproto_h_

#include <stdbool.h>

#define MULTIPROTO_REQ_END (-1)
#define MULTIPROTO_REQ_CONTINUE (-2)
#define MULTIPROTO_REQ_TIMER(us) (us)

typedef enum {
	MULTIPROTO_EV_START = 0,
	MULTIPROTO_EV_END,
	MULTIPROTO_EV_RADIO,
	MULTIPROTO_EV_TIMER,
	MULTIPROTO_COUNT_EV,
} MultiProtoEvent;

void multiproto_init();

int multiproto_callback(MultiProtoEvent event);

#endif
