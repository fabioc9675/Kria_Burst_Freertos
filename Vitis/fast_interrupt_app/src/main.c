#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xscugic.h"
#include "xparameters.h"
#include <stdio.h>

// --- CONFIGURACION ---
#define CORE_ID            0
#define PL_IRQ_ID          128U
#define INTC_DEVICE_ID     XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TEST_COUNT         50      // Mediremos el tiempo que toman 10 IRQs
#define INTER_COUNT        100      // 10ms

#define TICK_10MS     1000000 // A 100MHz

volatile uint64_t t_isr_entry = 0;
volatile uint64_t t_processing_done = 0;

// Prototipo de la tarea
void vBenchmarkTask(void *pvParameters);
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

// --- MANEJADOR DE INTERRUPCION (ISR) ---
void My_ISR_Handler(void *CallbackRef) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	interrupt_counter++;

	if (interrupt_counter % INTER_COUNT == 0) {
		// Liberamos el semaforo para la tarea de Benchmark
		xSemaphoreGiveFromISR(xIrqSemaphore, &xHigherPriorityTaskWoken);
	}
	// Cambio de contexto inmediato si es necesario
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

int main() {

	xIrqSemaphore = xSemaphoreCreateBinary();

	xTaskCreate(vBenchmarkTask, "Bench0", 8192, NULL, tskIDLE_PRIORITY + 5,
	NULL);

	vTaskStartScheduler();
	while (1)
		;
	return 0;
}

// --- TAREA 1: BENCHMARK DE INTERRUPCIONES ---
void vBenchmarkTask(void *pvParameters) {
	int cont_isr = 0;

	uint64_t freq = get_hw_freq();
	uint64_t t_start_wait, t_end_wait;
	uint64_t t_start_proc, t_end_proc;
	uint64_t t_start_total, t_end_total;

	t_start_total = get_hw_time();

	SetupInterruptSystem(&xInterruptController);

	xil_printf(
			"CORE,IRQ_ID,COUNT,TOTAL_COUNT,PROC_US,SLACK_US,FREE_CPU_PCT,TOTAL\r\n");

	for (int i = 0; i < TEST_COUNT; i++) {

		t_start_wait = get_hw_time();

		// A. Esperar Pulso del PL
		xSemaphoreTake(xIrqSemaphore, portMAX_DELAY);

		t_end_wait = get_hw_time();
		t_start_proc = get_hw_time();
		// --- SIMULACION DE CARGA DE AUDIO ---
		for (volatile int j = 0; j < 2000; j++)
			;
		t_end_proc = get_hw_time();

		uint64_t slack_us = ((t_end_wait - t_start_wait) * 1000000 / freq);
		uint64_t proc_us = ((t_end_proc - t_start_proc) * 1000000 / freq);

		cont_isr++;
		// El periodo total de Dave es 667 us
		// El porcentaje libre es (Slack / Periodo_Total) * 100
		float free_pct = ((float) slack_us / 667.0f) * 100.0f;

		t_end_total = get_hw_time();

		uint64_t total_t = ((t_end_total - t_start_total) * 1000000 / freq);

		t_start_total = get_hw_time();

		xil_printf("%d,%d,%d,%d,%d,%d,%d,%d\r\n", CORE_ID, PL_IRQ_ID, i,
				interrupt_counter, proc_us, slack_us, (int) free_pct, total_t);
		vTaskDelay(pdMS_TO_TICKS(2));

	}

	vTaskDelete(NULL);
}

int SetupInterruptSystem(XScuGic *GicInstPtr) {
	int Status;
	XScuGic_Config *IntcConfig;

	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == IntcConfig)
		return XST_FAILURE;

	// EL CORE 0 S HACE EL INITIALIZE (SOLO L)
	Status = XScuGic_CfgInitialize(GicInstPtr, IntcConfig,
			IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS)
		return XST_FAILURE;

	Status = XScuGic_Connect(GicInstPtr, PL_IRQ_ID,
			(Xil_ExceptionHandler) My_ISR_Handler, NULL);
	if (Status != XST_SUCCESS)
		return Status;

	XScuGic_SetPriorityTriggerType(GicInstPtr, PL_IRQ_ID, 0xA0, 0x3);

	// --- CONFIGURACION DE TARGET SEGURA (BYTE STEERING) ---
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

