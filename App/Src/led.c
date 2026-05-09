#include "led.h"


void led_task()
{
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_3);
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_4);
}