//-----------------------------------------------------------------------------
#include "blink.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
//-----------------------------------------------------------------------------
TaskHandle_t 	blink_task_handle = NULL;
uint32_t 		BlinkPattern = 0;
//-----------------------------------------------------------------------------
// Store blinking pattern in a task 32-bit notification value
void set_blink_pattern(uint32_t pattern)
{
	xTaskNotify(blink_task_handle, pattern, eSetValueWithOverwrite);
}
//-----------------------------------------------------------------------------
// Main task that do blinking job
void blink_task(void *arg)
{
	int	i, tick;
	uint32_t pattern = 0;

	while(1)
	{
		// Forever get task notification value (and don't clean up it)
		xTaskNotifyWait(0x00, 0x00, &BlinkPattern, 0);
		pattern = BlinkPattern;

		// Go though all bits and control led
		for (i=0; i<32; i++)
		{
			tick = pattern & 0x01;
			pattern = pattern >> 1;

			gpio_set_level(LED_PIN, tick);
			vTaskDelay(BLINK_TICK / portTICK_PERIOD_MS);
		}
	}

	vTaskDelete(NULL);
}
//-----------------------------------------------------------------------------
// Initialize LED pin and start task
void blink_start(uint32_t initial_pattern)
{
	gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

	xTaskCreate(blink_task, "blink_task", 4096, (void *)0, 5, &blink_task_handle);

	set_blink_pattern(initial_pattern);
}
//-----------------------------------------------------------------------------
