#ifndef COMMON_APIS_H
#define COMMON_APIS_H

#include <stdint.h>
#include <stdbool.h>

struct btl_common_apis
{
	void(*led_init)(void);
	void(*led_toggle)(uint32_t dly);
	void(*led_on)(void);
	void(*led_off)(void);
	void(*debug_uart_init)(void);
	void(*button_init)(void);
	bool(*get_btn_state)(void);
	void(*fpu_enable)(void);
	void(*timebase_init)(void);
};

extern struct btl_common_apis common_apis;

#endif /* COMMON_APIS_H */
