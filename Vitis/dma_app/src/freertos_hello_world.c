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

int vect_s[7] = { 1024, 2048, 4096, 8192, 16384, 32768, 48000 };

u16 RxBuffer[SAMPLES] __attribute__((aligned(64)));
XAxiDma AxiDma;                         // Instance of the DMA engine
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
    uint64_t freq = get_hw_freq();

    Status = init_dma();
    if (Status != XST_SUCCESS) {
        xil_printf("DMA Initialization Failed\r\n");
        vTaskDelete(NULL);
    }

    for (int i = 0; i < 7; i++) {
        // Calculamos tamaño real en BYTES
        int bytes_to_transfer = BUFFER_SIZE;

        // 1. LIMPIEZA DE CACHÉ ANTES DE INICIAR
        Xil_DCacheFlushRange((UINTPTR)RxBuffer, bytes_to_transfer);

        uint64_t t_i_start = get_hw_time();

        // 2. CORRECCIÓN DE DIRECCIÓN: XAXIDMA_DEVICE_TO_MEMORY
        Status = XAxiDma_SimpleTransfer(&AxiDma, (UINTPTR)RxBuffer,
                                        bytes_to_transfer, XAXIDMA_DEVICE_TO_DMA);

        if (Status != XST_SUCCESS) {
            xil_printf("DMA Transfer Failed %d\r\n", Status);
            break;
        }

        // 3. ESPERA ACTIVA SIN vTaskDelay (Para medir tiempo real de hardware)
        // Nota: En producción usaremos interrupciones, pero para medir latencia esto es mejor.
        while (XAxiDma_Busy(&AxiDma, XAXIDMA_DEVICE_TO_DMA)) {
            // Espera corta
        }

        // 4. INVALIDAR CACHÉ PARA LEER DATOS NUEVOS
        Xil_DCacheInvalidateRange((UINTPTR)RxBuffer, bytes_to_transfer);

        uint64_t t_i_end = get_hw_time() - t_i_start;
        float time = (float)(t_i_end * 1000000.0 / freq);

        xil_printf("N=%d samples (%d bytes), Time: %d.%02d us\r\n",
                  vect_s[i], bytes_to_transfer, (int)time, (int)((time-(int)time)*100));

        vTaskDelay(pdMS_TO_TICKS(500));
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
