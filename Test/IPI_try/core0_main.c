#include "FreeRTOS.h"
#include "task.h"
/* Xilinx includes. */
#include "xil_printf.h"
#include "xparameters.h"
#include "xipipsu.h"

#define IPI_DEVICE_ID XPAR_XIPIPSU_0_DEVICE_ID

XIpiPsu IpiInst;

void vIpiTask(void* Param);

int main(void) {

	xil_printf("Core0 Alive!\r\n");

	xTaskCreate(vIpiTask, "IPITask", tskIDLE_PRIORITY + 2048, NULL, 1, NULL);

	vTaskStartScheduler();
	while (1)
		;

	return 0;
}

void vIpiTask(void* Param) {

	XIpiPsu_Config *CfgPtr;

	// Inicializar IPI
	CfgPtr = XIpiPsu_LookupConfig(IPI_DEVICE_ID);
	XIpiPsu_CfgInitialize(&IpiInst, CfgPtr, CfgPtr->BaseAddress);

	xil_printf("Core 0: Presiona una tecla para mandar IPI al Core 1...\r\n");

	while (1) {
		char c = inbyte();
		outbyte(c);
		xil_printf("Core 0: Disparando IPI!\r\n");

		// Disparo de IPI
		XIpiPsu_TriggerIpi(&IpiInst, XPAR_PSU_IPI_0_BIT_MASK);

		vTaskDelay(pdMS_TO_TICKS(10));
	}
	vTaskDelete(NULL);
}
