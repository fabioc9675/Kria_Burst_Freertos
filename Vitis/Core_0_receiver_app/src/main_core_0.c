#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xscugic.h"
#include "xparameters.h"
#include "queue.h"
#include <stdio.h>

#include "shared_mem.h"

// --- CONFIGURACION ---
#define CORE_ID            0
#define PL_IRQ_ID          123U
#define INTC_DEVICE_ID     XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TEST_COUNT         100      // Mediremos el tiempo que toman 10 IRQs

#define TICK_10MS     1000000 // A 100MHz

// Usamos la instancia que FreeRTOS ya crea internamente
extern XScuGic xInterruptController;
SemaphoreHandle_t xIrqSemaphore = NULL;
QueueHandle_t xLogQueue = NULL;
volatile uint32_t interrupt_counter = 0;
volatile uint64_t t_isr_timestamp;

typedef struct {
	uint32_t time_read;
	uint32_t time_inter;
	uint32_t time_slack;
} TBenchmarkData;

// Functions to measure time
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

// Prototipo de funciones
void safe_log(const char* msg, uint32_t count);
int SetupInterruptSystem(XScuGic *GicInstPtr);

void vReadMemTask(void *pvParameters);
void vTaskMaster(void *pvParameters);
void vLoggerTask(void *pvParameters);

int main(void) {
	xil_printf("\r\nCore 0 alive\r\n");
	// 1. Inicializar Ring Buffer
	volatile uint8_t *pShared = (volatile uint8_t *) SHARED_MEM_BASE;
	for (size_t i = 0; i < sizeof(shared_log_t); i++) {
		pShared[i] = 0;
	}

	xIrqSemaphore = xSemaphoreCreateBinary();
	// IMPORTANTE: Crear la cola para 10 registros
	xLogQueue = xQueueCreate(10, sizeof(TBenchmarkData));

	// 2. Configurar su propia interrupciï¿½n (ID 121U para Core 0)
	SetupInterruptSystem(&xInterruptController);

	// 3. Crear Tarea de Benchmark propia
	xTaskCreate(vReadMemTask, "Bench0", 4096, NULL, tskIDLE_PRIORITY + 5,
	NULL);

	// 4. CREAR TAREA DE LOGGER (Solo en Core 0)
	xTaskCreate(vTaskMaster, "Logger", 2048, NULL, tskIDLE_PRIORITY + 3,
	NULL);

	xTaskCreate(vLoggerTask, "LoggerTask", 2048, NULL, tskIDLE_PRIORITY + 2,
	NULL);

	// xTaskCreate(vHealthMonitorTask, "HealthMonitor", 1024, NULL, tskIDLE_PRIORITY + 2, NULL);

	vTaskStartScheduler();
	while (1)
		;
	return 0;
}

/* **************************************************************************
 * **** FUNCIONES CONFIGURACION
 * **************************************************************************/

void vTaskMaster(void *pvParameters) {
	xil_printf("--- Ring Buffer Logger Started ---\r\n");

	// Inicializar punteros (Solo el Core 0 hace esto una vez)
	SHARED_LOG->head = 0;
	SHARED_LOG->tail = 0;
	Xil_DCacheFlushRange((UINTPTR) SHARED_LOG, sizeof(shared_log_t));

	while (1) {
		// Invalida la cachï¿½ de los punteros para ver si los esclavos movieron el 'head'
		Xil_DCacheInvalidateRange((UINTPTR) &SHARED_LOG->head,
				sizeof(uint32_t));
		Xil_DCacheInvalidateRange((UINTPTR) &SHARED_LOG->tail,
				sizeof(uint32_t));

		while (SHARED_LOG->tail != SHARED_LOG->head) {
			// Invalida la entrada especï¿½fica que vamos a leer
			volatile log_entry_t *entry = &SHARED_LOG->buffer[SHARED_LOG->tail];
			Xil_DCacheInvalidateRange((UINTPTR) entry, sizeof(log_entry_t));

			// Imprimir el mensaje
			xil_printf("%s", entry->text);
			// xil_printf("[CORE %d]: %s (Global Count: %u)\r\n", entry->core_id, entry->text, entry->counter);

			// Mover el puntero de lectura
			SHARED_LOG->tail = (SHARED_LOG->tail + 1) % LOG_BUFFER_SIZE;

			// Avisar a la cachï¿½ que movimos el tail (aunque los esclavos solo leen tail para ver si estï¿½ lleno)
			Xil_DCacheFlushRange((UINTPTR) &SHARED_LOG->tail, sizeof(uint32_t));
		}

		vTaskDelay(pdMS_TO_TICKS(100)); // Polling muy rï¿½pido pero eficiente
	}

	vTaskDelete(NULL);
}

void vReadMemTask(void *pvParameters) {
	TBenchmarkData log_entry;
	uint64_t t_last_irq = 0;
	uint64_t t_current_irq = 0;
	uint32_t freq = get_hw_freq();

	for (int i = 0; i < TEST_COUNT; i++) {
		// 1. Esperar Pulso del PL
		xSemaphoreTake(xIrqSemaphore, portMAX_DELAY);

		// Capturar el tiempo de la ISR actual y calcular intervalo entre IRQs
		t_current_irq = t_isr_timestamp;
		if (t_last_irq != 0) {
			log_entry.time_inter = t_current_irq - t_last_irq; // Debería dar ~10ms
		} else {
			log_entry.time_inter = 0;
		}

		// Slack: Tiempo desde que ocurrió la IRQ hasta que la tarea despertó
		log_entry.time_slack = get_hw_time() - t_current_irq;

		// 2. Simular fase de lectura/espera de 7ms
		uint64_t t_start_read = get_hw_time();

		// Cálculo de ciclos para 7ms: (Frecuencia * 0.007)
		uint64_t cycles_to_wait = (freq * 1) / 1000;
		uint64_t t_target = t_start_read + cycles_to_wait;

		while(get_hw_time() < t_target) {
			vTaskDelay(pdMS_TO_TICKS(2));

		}


		log_entry.time_read = get_hw_time() - t_start_read;

		// Guardar para el próximo cálculo
		t_last_irq = t_current_irq;

		xQueueSend(xLogQueue, &log_entry, 0);

		vTaskDelay(pdMS_TO_TICKS(2));
	}
	vTaskDelete(NULL);
}

void vLoggerTask(void *pvParameters) {

	TBenchmarkData data;
	uint32_t freq = get_hw_freq();
	char multi_line_buf[256];

	// 1. Encabezado actualizado: coincidencia exacta con las columnas del printf
	xil_printf("\nDATA_START\r\n");

	while (1) {
		if (xQueueReceive(xLogQueue, &data, portMAX_DELAY) == pdPASS) {

			// 2. Calculos de tiempo (usando double o float para precisiÃ³n)
			float t_read = (float) (data.time_read * 1000000.0 / freq);
			float t_inter = (float) (data.time_inter * 1000000.0 / freq);
			float t_slack = (float) (data.time_slack * 1000000.0 / freq);

			// Construimos el bloque completo usando snprintf
			snprintf(multi_line_buf, sizeof(multi_line_buf),
					"Core %u,\tRead: %.2f (us), \tInter: %.2f (us), \tSlack: %.2f (us)\r\n",
					CORE_ID, t_read, t_inter, t_slack);

			// Lo enviamos a la memoria compartida
			safe_log(multi_line_buf, interrupt_counter);
		}
	}

}

/* **************************************************************************
 * **** FUNCIONES CONFIGURACION
 * **************************************************************************/
// --- MANEJADOR DE INTERRUPCION (ISR) ---
void My_ISR_Handler(void *CallbackRef) {
	t_isr_timestamp = get_hw_time(); // Captura el momento exacto del hardware

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	// counter
	interrupt_counter++;

	// Liberamos el semaforo para la tarea de Benchmark
	xSemaphoreGiveFromISR(xIrqSemaphore, &xHigherPriorityTaskWoken);

	// Cambio de contexto inmediato si es necesario
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// --- CONFIGURACION DE INTERRUPCIONES (CORREGIDA) ---
int SetupInterruptSystem(XScuGic *GicInstPtr) {

	int Status;
	XScuGic_Config *IntcConfig;

	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == IntcConfig)
		return XST_FAILURE;

	Status = XScuGic_CfgInitialize(GicInstPtr, IntcConfig,
			IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS)
		return XST_FAILURE;

	// Conectar el ID 121 a nuestra ISR
	Status = XScuGic_Connect(GicInstPtr, PL_IRQ_ID,
			(Xil_ExceptionHandler) My_ISR_Handler, NULL);
	if (Status != XST_SUCCESS)
		return Status;

	XScuGic_SetPriorityTriggerType(GicInstPtr, PL_IRQ_ID, 0xA0, 0x3);

	// --- NUEVO: DIRECCIONAMIENTO AL CORE ESPECï¿½FICO ---
	// Esto asegura que la interrupciï¿½n 122 vaya al Core 1, etc.
	u32 CpuMask = (1 << CORE_ID);
	u32 TargetReg = XScuGic_DistReadReg(GicInstPtr,
			XSCUGIC_SPI_TARGET_OFFSET_CALC(PL_IRQ_ID));
	TargetReg = (TargetReg & ~0xFF) | CpuMask;
	XScuGic_DistWriteReg(GicInstPtr, XSCUGIC_SPI_TARGET_OFFSET_CALC(PL_IRQ_ID),
			TargetReg);

	XScuGic_Enable(GicInstPtr, PL_IRQ_ID);
	return XST_SUCCESS;

}

/* **************************************************************************
 * **** FUNCIONES ACCESORIAS
 * **************************************************************************/

void safe_log(const char* msg, uint32_t count) {
	uint32_t next_head = (SHARED_LOG->head + 1) % LOG_BUFFER_SIZE;

	if (next_head != SHARED_LOG->tail) {
		volatile log_entry_t *entry = &SHARED_LOG->buffer[SHARED_LOG->head];

		// Copiamos el bloque de texto formateado
		strncpy((char *) entry->text, msg, MSG_LEN - 1);
		entry->text[MSG_LEN - 1] = '\0'; // Asegurar cierre de string
		entry->core_id = CORE_ID;
		entry->counter = count;

		// Limpiar cache para que el Master vea el mensaje
		Xil_DCacheFlushRange((UINTPTR) entry, sizeof(log_entry_t));

		// Actualizar el puntero head en RAM
		SHARED_LOG->head = next_head;
		Xil_DCacheFlushRange((UINTPTR) &SHARED_LOG->head, sizeof(uint32_t));
	}
}

