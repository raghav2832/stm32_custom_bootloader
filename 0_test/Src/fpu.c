#include "stm32f4xx.h"

void fpu_enable(void){
	SCB->CPACR |= (15U << 20);
}
