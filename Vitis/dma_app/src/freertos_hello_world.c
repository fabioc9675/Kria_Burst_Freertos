#include "xaxidma.h"
#include "xscugic.h"
#include "xparameters.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_printf.h"
#include "xil_cache.h"

#define DMA_OUT_DEV_ID XPAR_AXI_DMA_OUT_DEVICE_ID
#define DMA_OUT_IRQ_ID XPAR_FABRIC_AXI_DMA_OUT_S2MM_INTROUT_INTR

#define DMA_IN_DEV_ID XPAR_AXI_DMA_IN_DEVICE_ID
#define DMA_IN_IRQ_ID XPAR_FABRIC_AXI_DMA_IN_S2MM_INTROUT_INTR

#define SAMPLES 48000
#define BUFFER_SIZE (SAMPLES * 2)

int vect_s[7] = { 1024, 2048, 4096, 8192, 16384, 32768, 48000 };

u16 RxBuffer[SAMPLES] __attribute__((aligned(64)));
XAxiDma AxiDmaOut;                         // Instance of the DMA engine
XAxiDma AxiDmaIn;                          // Instance of the DMA engine
XScuGic InterruptController;            // Instance of the Interrupt Controller
SemaphoreHandle_t xSemaphoreDMA = NULL; // Semaphore for DMA completion

// --- HELPERS TIEMPO ARM A53 ---
static inline uint64_t get_hw_time(void) {
	uint64_t val;
	asm volatile("mrs %0, cntpct_el0" : "=r" (val));
	return val;
}

static inline uint32_t get_hw_freq(void) {
	uint32_t val;
	asm volatile("mrs %0, cntfrq_el0" : "=r" (val));
	return val;
}

int init_dma_out();
int init_dma_in();
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
	uint64_t freq = get_hw_freq();

	Status = init_dma_out();
	if (Status != XST_SUCCESS) {
		xil_printf("DMA OUT Initialization Failed\r\n");
		vTaskDelete(NULL);
	}

	Status = init_dma_in();
	if (Status != XST_SUCCESS) {
		xil_printf("DMA IN Initialization Failed\r\n");
		vTaskDelete(NULL);
	}

	// FIJAMOS EL TAMAÑO: 48000 muestras de 16 bits = 96000 bytes
	const int FIXED_SAMPLES = 48000;
	const int BYTES_TO_TRANSFER = FIXED_SAMPLES * sizeof(u16);

	xil_printf("Iniciando test de tamano fijo: %d bytes\r\n", SAMPLES);

	while (1) { // Bucle infinito para probar la estabilidad

		// 1. Limpiar caché: Aseguramos que la RAM esté lista para recibir
		Xil_DCacheFlushRange((UINTPTR) RxBuffer, BYTES_TO_TRANSFER);

		uint64_t t_i_start = get_hw_time();

		// 2. Iniciar transferencia
		Status = XAxiDma_SimpleTransfer(&AxiDmaOut, (UINTPTR) RxBuffer,
				BYTES_TO_TRANSFER, XAXIDMA_DEVICE_TO_DMA);

		if (Status != XST_SUCCESS) {
			xil_printf("Error al iniciar: %d\r\n", Status);
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		// 3. Espera activa (Si se queda aquí, la PL NO está mandando TLAST)
		// Agregamos un pequeño timeout de seguridad para que no muera el procesador
		int timeout = 0;
		while (XAxiDma_Busy(&AxiDmaOut, XAXIDMA_DEVICE_TO_DMA)) {
			timeout++;
			if (timeout > 10000000) { // Timeout arbitrario
				xil_printf(
						"TIMEOUT: El DMA sigue ocupado. ¿La PL envió TLAST?\r\n");
				break;
			}
		}

		// 4. Invalidar caché: Obligamos al CPU a leer de la RAM, no de su caché
		Xil_DCacheInvalidateRange((UINTPTR) RxBuffer, BYTES_TO_TRANSFER);

		uint64_t t_i_end = get_hw_time() - t_i_start;
		float time = (float) (t_i_end * 1000000.0 / freq);

//        for (int i = 0; i < 1000; i++){
//        	xil_printf("%d,", (short)RxBuffer[i]);
//        }

		// 1. Limpiar todo el buffer
		memset(RxBuffer, 0, BYTES_TO_TRANSFER);

		for (int i = 0; i < SAMPLES; i++) {
			RxBuffer[i] = 123;
		}

		// ¡ESTO ES LO QUE FALTA!
		Xil_DCacheFlushRange((UINTPTR) RxBuffer, BYTES_TO_TRANSFER);

		t_i_start = get_hw_time();
		Status = XAxiDma_SimpleTransfer(&AxiDmaIn, (UINTPTR) RxBuffer,
				BYTES_TO_TRANSFER, XAXIDMA_DMA_TO_DEVICE);

		if (Status != XST_SUCCESS) {
			xil_printf("Error al iniciar: %d\r\n", Status);
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		// 3. Espera activa (Si se queda aquí, la PL NO está mandando TLAST)
		// Agregamos un pequeño timeout de seguridad para que no muera el procesador
		timeout = 0;
		while (XAxiDma_Busy(&AxiDmaIn, XAXIDMA_DMA_TO_DEVICE)) {
			timeout++;
			if (timeout > 10000000) { // Timeout arbitrario
				xil_printf(
						"TIMEOUT: El DMA sigue ocupado. ¿La PL envió TLAST?\r\n");
				break;
			}
		}

		// 4. Invalidar caché: Obligamos al CPU a leer de la RAM, no de su caché
		Xil_DCacheInvalidateRange((UINTPTR) RxBuffer, BYTES_TO_TRANSFER);

		t_i_end = get_hw_time() - t_i_start;
		float time_in = (float) (t_i_end * 1000000.0 / freq);

		xil_printf(
				"Transferencia OK. Tiempo Entrada: %d us, \tTiempo Salida: %d us\r\n",
				(int) time, (int) time_in);

		vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 seg entre pruebas
	}
}

int init_dma_out() {
	XAxiDma_Config *CfgPtr;
	int Status;

	CfgPtr = XAxiDma_LookupConfig(DMA_OUT_DEV_ID);
	if (!CfgPtr) {
		xil_printf("No config found for %d\r\n", DMA_OUT_DEV_ID);
		return XST_FAILURE;
	}

	Status = XAxiDma_CfgInitialize(&AxiDmaOut, CfgPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed %d\r\n", Status);
		return XST_FAILURE;
	}

	if (AxiDmaOut.HasS2Mm) {
		xil_printf("Canal S2MM detectado y listo.\r\n");
	} else {
		xil_printf("Error: El hardware DMA no tiene el canal S2MM activo.\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

int init_dma_in() {
	XAxiDma_Config *CfgPtr;
	int Status;

	CfgPtr = XAxiDma_LookupConfig(DMA_IN_DEV_ID);
	if (!CfgPtr) {
		xil_printf("No config found for %d\r\n", DMA_IN_DEV_ID);
		return XST_FAILURE;
	}

	Status = XAxiDma_CfgInitialize(&AxiDmaIn, CfgPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed %d\r\n", Status);
		return XST_FAILURE;
	}

	if (AxiDmaIn.HasMm2S) {
		xil_printf("Canal MM2S detectado y listo.\r\n");
	} else {
		xil_printf("Error: El hardware DMA no tiene el canal MM2S activo.\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}
