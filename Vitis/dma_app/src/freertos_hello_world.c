#include "xaxidma.h"
#include "xscugic.h"
#include "xparameters.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_printf.h"
#include "xil_cache.h"

#define DMA_DEV_ID XPAR_AXIDMA_0_DEVICE_ID
#define DMA_IRQ_ID XPAR_FABRIC_AXIDMA_0_VEC_ID

#define SAMPLES 48000
#define BUFFER_SIZE (SAMPLES * 2)

u16 RxBuffer[SAMPLES] __attribute__((aligned(64)));
XAxiDma AxiDma;                         // Instance of the DMA engine
XScuGic InterruptController;            // Instance of the Interrupt Controller
SemaphoreHandle_t xSemaphoreDMA = NULL; // Semaphore for DMA completion

int init_dma();
void dma_transfer_task(void *pvParameters);

int main(void) {
	xil_printf("FreeRTOS DMA Hello World Example\r\n");

	// Create the DMA transfer task
	xTaskCreate(dma_transfer_task, "DMA_Transfer_Task", 2048, NULL,
	tskIDLE_PRIORITY + 1, NULL);

	// Start the FreeRTOS scheduler
	vTaskStartScheduler();

	// Should never reach here
	for (;;)
		;
	return 0;
}

void dma_transfer_task(void *pvParameters) {
	int Status;
	xil_printf("DMA Transfer Task Started\r\n");

	Status = init_dma();
	if (Status != XST_SUCCESS) {
		xil_printf("DMA Initialization Failed\r\n");
		vTaskDelete(NULL);
	}

	memset(RxBuffer, 0, BUFFER_SIZE);
	Xil_DCacheFlushRange((UINTPTR) RxBuffer, BUFFER_SIZE);

	while (1) {
		// Start the DMA transfer
		Status = XAxiDma_SimpleTransfer(&AxiDma, (UINTPTR) RxBuffer,
		BUFFER_SIZE, XAXIDMA_DEVICE_TO_DMA);

		if (Status != XST_SUCCESS) {
			xil_printf("DMA Transfer Failed %d\r\n", Status);
			vTaskDelete(NULL);
		}

		// Wait for the DMA transfer to complete
		xil_printf("Waiting for DMA transfer to complete...\r\n");
		while (XAxiDma_Busy(&AxiDma, XAXIDMA_DEVICE_TO_DMA)) {
			vTaskDelay(pdMS_TO_TICKS(1));
		}

		Xil_DCacheInvalidateRange((UINTPTR) RxBuffer, BUFFER_SIZE);

		xil_printf("DMA Transfer Completed\r\n");

		// Process the received data (for demonstration, just print the first 10 samples)
		xil_printf("First 1000 samples received:\r\n");
		for (int i = 0; i < SAMPLES; i++) {
			xil_printf("%d, ", (short) RxBuffer[i]);
		}

		xil_printf("\r\n--- Test Completed ---\r\n");

		vTaskDelay(pdMS_TO_TICKS(20000)); // Delay before next transfer
	}

	vTaskDelete(NULL);
}

int init_dma() {
	XAxiDma_Config *CfgPtr;
	int Status;

	CfgPtr = XAxiDma_LookupConfig(DMA_DEV_ID);
	if (!CfgPtr) {
		xil_printf("No config found for %d\r\n", DMA_DEV_ID);
		return XST_FAILURE;
	}

	Status = XAxiDma_CfgInitialize(&AxiDma, CfgPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed %d\r\n", Status);
		return XST_FAILURE;
	}

	if (AxiDma.HasS2Mm) {
		xil_printf("Canal S2MM detectado y listo.\r\n");
	} else {
		xil_printf("Error: El hardware DMA no tiene el canal S2MM activo.\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}
