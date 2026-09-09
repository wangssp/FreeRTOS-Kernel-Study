#include "FreeRTOS.h"
#include "task.h"

void vTaskFunction( void *pvParameters )
{
    for( ;; )
    {
        // 任务代码
    }
}

int main( void )
{
    xTaskCreate(
        vTaskFunction,
        "Task",
        configMINIMAL_STACK_SIZE,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    vTaskStartScheduler();

    for( ;; );
}
