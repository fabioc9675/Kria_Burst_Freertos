#include "FreeRTOS.h"
#include "task.h"
/* Xilinx includes. */
#include "xil_printf.h"
#include "xparameters.h"
#include "xipipsu.h"
#include "xgpio.h"
#include "xil_exception.h"

#define IPI_DEVICE_ID    XPAR_XIPIPSU_0_DEVICE_ID
#define IPI_INT_ID       XPAR_XIPIPSU_0_INT_ID
#define GPIO_DEVICE_ID   XPAR_GPIO_0_DEVICE_ID

XIpiPsu IpiInst;
XGpio GpioInst;

void Ipi_Intr_Handler(void *data);
void vIpiTask(void* Param);

int main(void) {

	xil_printf("Core1 Alive!\r\n");

	xTaskCreate(vIpiTask, "IPITask", tskIDLE_PRIORITY + 2048, NULL, 1, NULL);

	vTaskStartScheduler();
	while (1)
		;

	return 0;
}

void vIpiTask(void* Param) {

	XGpio_Initialize(&GpioInst, GPIO_DEVICE_ID);
	XGpio_SetDataDirection(&GpioInst, 1, 0x0); // Salida

	// Inicializar IPI
	XIpiPsu_Config *CfgPtr = XIpiPsu_LookupConfig(IPI_DEVICE_ID);
	XIpiPsu_CfgInitialize(&IpiInst, CfgPtr, CfgPtr->BaseAddress);

	// 1. Instalar el handler en el GIC gestionado por FreeRTOS
	xPortInstallInterruptHandler(IPI_INT_ID,
			(XInterruptHandler) Ipi_Intr_Handler, (void *) &IpiInst);

	vPortEnableInterrupt(IPI_INT_ID);

	xil_printf("Core 1: Sistema IPI listo y esperando...\r\n");

	xil_printf("Core 1: Intentando auto-interrupcion...\r\n");
	vTaskDelay(pdMS_TO_TICKS(1000));

	XIpiPsu_TriggerIpi(&IpiInst, XPAR_PSU_IPI_0_BIT_MASK);

	while (1) {

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	vTaskDelete(NULL);
}

void Ipi_Intr_Handler(void *data) {
	XIpiPsu_ClearInterruptStatus(&IpiInst, XPAR_PSU_IPI_0_BIT_MASK);

	int led_state = XGpio_DiscreteRead(&GpioInst, 1);
	XGpio_DiscreteWrite(&GpioInst, 1, !led_state);

	xil_printf("Core 1: IPI recibido de Core 0 - LED: %d\r\n", led_state);
}

