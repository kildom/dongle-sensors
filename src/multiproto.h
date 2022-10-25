#ifndef _multiproto_h_
#define _multiproto_h_

#include <stdbool.h>

void multiproto_init();

// callbacks
bool multiproto_radio_callback();
bool multiproto_start_callback();
bool multiproto_end_callback();

#endif
