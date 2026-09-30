#include <stdio.h>
#include "stm32f4xx.h"
#include "fpu.h"
#include "uart.h"
#include "timebase.h"
#include "bsp.h"

/*Modules:
 * FPU
 * UART
 * TIMEBASE
 * GPIO (BSP)
 * ADC
 * */


typedef void(*func_ptr)(void);

typedef enum
{
	APP1 = 1,
	FACTORY_APP
}SYS_APPS;

static void uart_callback(void);
static void process_btldr_cmds(SYS_APPS);
static void jump_to_app(uint32_t);

#define  GPIOAEN		(1U<<0)
#define  PIN5			(1U<<5)
#define  LED_PIN		PIN5

#define MSP_VERIFY_MASK			    0x2FFE0000
#define EMPTY_MEM					0xFFFFFFFF

#define MEM_CHECK_V2

#define SECTOR0_BASE_ADDRESS        0x08000000  /*Bootloader Sector*/
#define SECTOR1_BASE_ADDRESS        0x08004000  /*Default Application Sector*/
#define SECTOR2_BASE_ADDRESS        0x08008000  /*Application 1 Sector*/
#define SECTOR3_BASE_ADDRESS        0x0800C000  /*Factory Application Sector*/

#define DEFAULT_APP_ADDRESS         SECTOR1_BASE_ADDRESS
#define APP1_ADDRESS                SECTOR2_BASE_ADDRESS
#define FACTORY_APP_ADDRESS         SECTOR3_BASE_ADDRESS

bool btn_state;
volatile char    g_ch_key;
volatile uint8_t g_ui_key;


static void jump_to_app(uint32_t addr_value)
{
	uint32_t app_start_address;
	func_ptr jump_to_app;


	/*Version 1*/
    #ifdef MEM_CHECK_V1
	if(((*(uint32_t *)addr_value) & MSP_VERIFY_MASK ) ==  0x20020000)
    #endif

    #ifdef MEM_CHECK_V2
	/*Version 2*/
	if((*(uint32_t *)addr_value) != EMPTY_MEM)
    #endif

	{
		printf("Starting application.....\n\r");
		app_start_address =  *(uint32_t *)(addr_value + 4);

		jump_to_app = (func_ptr) app_start_address;

		/*Initialialize main stack pointer*/
		__set_MSP(*(uint32_t *)addr_value);

		/*jump*/
		jump_to_app();

	}
	else
	{
		printf("No application found at location %08lX...\n\r", addr_value);
	}


}

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

struct btl_common_apis common_apis  __attribute__((section(".COMMON_APIS")))= {
		led_init,
		led_toggle,
		led_on,
		led_off,
		debug_uart_init,
		button_init,
		get_btn_state,
		fpu_enable,
		timebase_init

};

int main()
{
	/*Enable FPU*/
	fpu_enable();

	/*Initialize debug UART*/
	debug_uart_init();

	/*Initialize timebase*/
	timebase_init();

	/*Initialize LED*/
	led_init();

	/*Initialize Push button*/

	button_init();

	//jmp_to_default_app();
	if(get_btn_state())
	{
		/*Button is presses*/
		printf("Button Pressed \n\r");

		printf("===============================================\n\r");
		printf("===============================================\n\r");
		printf("===============================================\n\r");
		printf("===============================================\n\r");

		printf("===============================================\n\r");
		printf("\n\r");
		printf("Bootloader Menu\n\r");
		printf("\n");
		printf("===============================================\n\r");
		printf("===============================================\n\r");
		printf("===============================================\n\r");

		printf("Available Commands:\n\r");
		printf("1       ==> Run App 1\n\r");
		printf("F       ==> Factory App 2\n\r");
		printf("Any key ==> Run Default App\n\r");

		while(1)
		{
			/*Process bootloader commands*/
			process_btldr_cmds(g_ui_key);
		}

	}
	else
	{
		/*Button not pressed */
		jump_to_app(DEFAULT_APP_ADDRESS);
	}

	while(1)
	{

	}
}

static void process_btldr_cmds(SYS_APPS curr_app)
{
	switch(curr_app)
	{
	case APP1 :
		printf("APP1 selected...\n\r");
		jump_to_app(APP1_ADDRESS);
		break;
	case FACTORY_APP :
		printf("FACTORY APP selected...\n\r");
		jump_to_app(FACTORY_APP_ADDRESS);
		break;
	default :
		break;
	}
}

static void uart_callback(void){

	g_ch_key = USART2->DR;

	if(g_ch_key == '1')
	{
		g_ui_key = 1;
	}
	else if((g_ch_key == 'F') || (g_ch_key == 'f'))
	{
		g_ui_key = 2;
	}
	else
	{

	}
}

void USART2_IRQHandler(void){

	/*Check if RXNE is set*/
	if(USART2->SR & SR_RXNE){
		uart_callback();
	}
}









