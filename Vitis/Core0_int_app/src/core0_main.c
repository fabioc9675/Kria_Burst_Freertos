#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xscugic.h"
#include "xparameters.h"
#include <stdio.h>

#include "shared_mem.h"

// --- CONFIGURACION ---
#define CORE_ID            0
#define PL_IRQ_ID          123U
#define INTC_DEVICE_ID     XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TEST_COUNT         100      // Mediremos el tiempo que toman 10 IRQs

#define TICK_10MS     1000000 // A 100MHz

// Prototipo de la tarea
void vBenchmarkTask(void *pvParameters);
void vTaskMaster(void *pvParameters);
int SetupInterruptSystem(XScuGic *GicInstPtr);

// --- INSTANCIAS ---
// Usamos la instancia que FreeRTOS ya crea internamente
extern XScuGic xInterruptController;
SemaphoreHandle_t xIrqSemaphore = NULL;
QueueHandle_t xLogQueue = NULL;
volatile uint32_t interrupt_counter = 0;

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

void safe_log(const char* msg, uint32_t count) {
	uint32_t next_head = (SHARED_LOG->head + 1) % LOG_BUFFER_SIZE;

	if (next_head != SHARED_LOG->tail) {
		volatile log_entry_t *entry = &SHARED_LOG->buffer[SHARED_LOG->head];

		// Copiamos el bloque de texto formateado
		strncpy((char *) entry->text, msg, MSG_LEN - 1);
		entry->text[MSG_LEN - 1] = '\0'; // Asegurar cierre de string
		entry->core_id = CORE_ID;
		entry->counter = count;

		Xil_DCacheFlushRange((UINTPTR) entry, sizeof(log_entry_t));

		// Actualizar el puntero head en RAM
		SHARED_LOG->head = next_head;
		Xil_DCacheFlushRange((UINTPTR) &SHARED_LOG->head, sizeof(uint32_t));
	}
}

// --- MANEJADOR DE INTERRUPCIÓN (ISR) ---
void My_ISR_Handler(void *CallbackRef) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	interrupt_counter++;

	if (interrupt_counter % TEST_COUNT == 0) {
		// Liberamos el semáforo para la tarea de Benchmark
		xSemaphoreGiveFromISR(xIrqSemaphore, &xHigherPriorityTaskWoken);
	}
	// Cambio de contexto inmediato si es necesario
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

int main() {

	volatile uint8_t *pShared = (volatile uint8_t *) SHARED_MEM_BASE;
	for (size_t i = 0; i < sizeof(shared_log_t); i++) {
		pShared[i] = 0;
	}

	xIrqSemaphore = xSemaphoreCreateBinary();

	xTaskCreate(vBenchmarkTask, "Bench0", 8192, NULL, tskIDLE_PRIORITY + 5,
	NULL);

	xTaskCreate(vTaskMaster, "Logger", 2048, NULL, tskIDLE_PRIORITY + 6,
	NULL);

	SHARED_LOG->is_ready = 0xABCDEF01;
	Xil_DCacheFlushRange((UINTPTR) SHARED_LOG, sizeof(shared_log_t));

	vTaskStartScheduler();
	while (1)
		;
	return 0;
}

// --- TAREA 1: BENCHMARK DE INTERRUPCIONES ---
void vBenchmarkTask(void *pvParameters) {
	int cont_isr = 0;
	char multi_line_buf[256];

	SetupInterruptSystem(&xInterruptController);

	for (int i = 0; i < TEST_COUNT; i++) {
		// A. Esperar Pulso del PL
		xSemaphoreTake(xIrqSemaphore, portMAX_DELAY);

		cont_isr++;

		// Construimos el bloque completo usando snprintf
		snprintf(multi_line_buf, sizeof(multi_line_buf),
				"Int CORE %d, cont = %d\r\n", CORE_ID, cont_isr);

		// Lo enviamos a la memoria compartida
		safe_log(multi_line_buf, interrupt_counter);

		vTaskDelay(pdMS_TO_TICKS(2));

	}

	vTaskDelete(NULL);
}

void vTaskMaster(void *pvParameters) {
	xil_printf("--- Ring Buffer Logger Started ---\r\n");

	SHARED_LOG->head = 0;
	SHARED_LOG->tail = 0;
	Xil_DCacheFlushRange((UINTPTR) SHARED_LOG, sizeof(shared_log_t));

	while (1) {
		Xil_DCacheInvalidateRange((UINTPTR) &SHARED_LOG->head,
				sizeof(uint32_t));
		Xil_DCacheInvalidateRange((UINTPTR) &SHARED_LOG->tail,
				sizeof(uint32_t));

		while (SHARED_LOG->tail != SHARED_LOG->head) {
			volatile log_entry_t *entry = &SHARED_LOG->buffer[SHARED_LOG->tail];
			Xil_DCacheInvalidateRange((UINTPTR) entry, sizeof(log_entry_t));

			xil_printf("%s", entry->text);

			SHARED_LOG->tail = (SHARED_LOG->tail + 1) % LOG_BUFFER_SIZE;

			Xil_DCacheFlushRange((UINTPTR) &SHARED_LOG->tail, sizeof(uint32_t));
		}

		vTaskDelay(pdMS_TO_TICKS(10)); //
	}
}

int SetupInterruptSystem(XScuGic *GicInstPtr) {
	int Status;
	XScuGic_Config *IntcConfig;

	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == IntcConfig)
		return XST_FAILURE;

	// EL CORE 0 SÍ HACE EL INITIALIZE (SOLO ÉL)
	Status = XScuGic_CfgInitialize(GicInstPtr, IntcConfig,
			IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS)
		return XST_FAILURE;

	Status = XScuGic_Connect(GicInstPtr, PL_IRQ_ID,
			(Xil_ExceptionHandler) My_ISR_Handler, NULL);
	if (Status != XST_SUCCESS)
		return Status;

	XScuGic_SetPriorityTriggerType(GicInstPtr, PL_IRQ_ID, 0xA0, 0x3);

	// --- CONFIGURACIÓN DE TARGET SEGURA (BYTE STEERING) ---
	u32 Mask = (1 << CORE_ID); // Para Core 0 es 0x01
	u32 Offset = PL_IRQ_ID % 4;
	u32 Shift = Offset * 8;     // Para la 123, el desplazamiento es 24 bits

	u32 TargetReg = XScuGic_DistReadReg(GicInstPtr,
			XSCUGIC_SPI_TARGET_OFFSET_CALC(PL_IRQ_ID));

	// Limpiamos SOLO el byte de la IRQ 123 y ponemos el bit del Core 0
	TargetReg &= ~(0xFF << Shift);
	TargetReg |= (Mask << Shift);

	XScuGic_DistWriteReg(GicInstPtr, XSCUGIC_SPI_TARGET_OFFSET_CALC(PL_IRQ_ID),
			TargetReg);

	XScuGic_Enable(GicInstPtr, PL_IRQ_ID);
	return XST_SUCCESS;
}

