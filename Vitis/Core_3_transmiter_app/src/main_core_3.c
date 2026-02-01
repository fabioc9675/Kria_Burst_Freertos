#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xaxidma.h"
#include "xscugic.h"
#include "xparameters.h"
#include "queue.h"
#include <stdio.h>

#include "shared_mem.h"

// --- DMA de salida desde el PL, entrada al PS ---
#define DMA_IN_DEV_ID XPAR_AXI_DMA_IN_DEVICE_ID
#define DMA_IN_IRQ_ID XPAR_FABRIC_AXI_DMA_IN_S2MM_INTROUT_INTR
#define SAMPLES 48000
#define BUFFER_SIZE (SAMPLES * 2)

// --- CONFIGURACION ---
#define CORE_ID            3
#define PL_IRQ_ID          126U
#define INTC_DEVICE_ID     XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TEST_COUNT         100      // Mediremos el tiempo que toman 10 IRQs

#define TICK_10MS     1000000 // A 100MHz

// Usamos la instancia que FreeRTOS ya crea internamente
extern XScuGic xInterruptController;
SemaphoreHandle_t xIrqSemaphore = NULL;
QueueHandle_t xLogQueue = NULL;
volatile uint32_t interrupt_counter = 0;
volatile uint64_t t_isr_timestamp;

// Variables de la DMA
// u32 RxBuffer[SAMPLES] __attribute__((aligned(64))); // Cambiar a u32
XAxiDma AxiDmaIn;

typedef struct {
	uint64_t real_tick;
	uint16_t error;
	uint32_t checksum;
	uint32_t time_read;
	uint32_t time_inter;
	uint32_t time_slack;
	uint32_t time_dma;
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
int init_dma_in();
void safe_log(const char* msg, uint32_t count);
int SetupInterruptSystem(XScuGic *GicInstPtr);

void vReadMemTask(void *pvParameters);
void vLoggerTask(void *pvParameters);

int main(void) {
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

void vReadMemTask(void *pvParameters) {
	TBenchmarkData log_entry;
	uint64_t t_last_irq = 0;
	uint64_t t_current_irq = 0;
	uint32_t freq = get_hw_freq();
	int cont = 0;
	int Status;
	log_entry.error = 0;

	Status = init_dma_in();
	if (Status != XST_SUCCESS) {
		// xil_printf("DMA OUT Initialization Failed\r\n");
		vTaskDelete(NULL);
	}

	// FIJAMOS EL TAMAÑO: 48000 muestras de 16 bits = 96000 bytes
	const int FIXED_SAMPLES = 48000;
	const int BYTES_TO_TRANSFER = FIXED_SAMPLES * 4;

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
		uint64_t cycles_to_wait = (freq * 7) / 1000;
		uint64_t t_target = t_start_read + cycles_to_wait;

		while (get_hw_time() < t_target) {
			vTaskDelay(pdMS_TO_TICKS(2));
		}
		// captura del tick real
		log_entry.real_tick = get_hw_time();

		log_entry.time_read = get_hw_time() - t_start_read;

		// Apuntamos a la memoria compartida
		uint32_t r_idx = DATA_PIPELINE->read_idx;
		while (DATA_PIPELINE->packets[r_idx].status != BUF_READY)
			;
		UINTPTR shared_addr = (UINTPTR) DATA_PIPELINE->packets[r_idx].payload;

		//--------------------------------------------------
		// Lectura del DMA
		//--------------------------------------------------
		Xil_DCacheInvalidateRange(shared_addr, BYTES_TO_TRANSFER);

		// 4. Verificacion de Checksum
		uint32_t calc_sum = DATA_PIPELINE->packets[r_idx].payload[0] +
		DATA_PIPELINE->packets[r_idx].payload[1] +
		DATA_PIPELINE->packets[r_idx].payload[47997] +
		DATA_PIPELINE->packets[r_idx].payload[47998];
		log_entry.checksum = calc_sum;

		if (calc_sum != DATA_PIPELINE->packets[r_idx].checksum) {
			// Error de integridad (puedes contar errores aqui para el log)
			log_entry.error += 1;
		}

		// 5. Modificacion de datos (Inyectar el valor 123 para el monitor)
		if (cont % 3 == 1) {
			DATA_PIPELINE->packets[r_idx].payload[100] = 123;
		}

		// Flush obligatorio antes de disparar el DMA de salida
		Xil_DCacheFlushRange(shared_addr, BYTES_TO_TRANSFER);

		uint32_t t_i_start = get_hw_time();
		Status = XAxiDma_SimpleTransfer(&AxiDmaIn, shared_addr,
				BYTES_TO_TRANSFER, XAXIDMA_DMA_TO_DEVICE);

		if (Status != XST_SUCCESS) {
			xil_printf("Error al iniciar: %d\r\n", Status);
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		// 3. Espera activa (Si se queda aquí, la PL NO está mandando TLAST)
		// Agregamos un pequeño timeout de seguridad para que no muera el procesador
		uint32_t timeout = 0;
		while (XAxiDma_Busy(&AxiDmaIn, XAXIDMA_DMA_TO_DEVICE)) {
			timeout++;
			if (timeout > 10000000) { // Timeout arbitrario
				xil_printf(
						"TIMEOUT: El DMA sigue ocupado. La PL envio TLAST?\r\n");
				break;
			}
		}

		DATA_PIPELINE->packets[r_idx].status = BUF_EMPTY;
		DATA_PIPELINE->read_idx = (r_idx + 1) % NUM_BUFFERS;

		// 4. Invalidar caché: Obligamos al CPU a leer de la RAM, no de su caché
		Xil_DCacheInvalidateRange(shared_addr, BYTES_TO_TRANSFER);

		uint32_t t_i_end = get_hw_time() - t_i_start;

		log_entry.time_dma = t_i_end;

		cont++;
		//-------------------------------------------
		// Fin de DMA
		//-------------------------------------------

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
	//	xil_printf("\nDATA_START\r\n");
	//	xil_printf("Core,tick,init_op,memory,interrupt,slack\r\n");

	while (1) {
		if (xQueueReceive(xLogQueue, &data, portMAX_DELAY) == pdPASS) {

			// 2. Calculos de tiempo (usando double o float para precision)
			long real_tick = data.real_tick;
			int error = data.error;
			long checksum = data.checksum;
			float t_read = (float) (data.time_read * 1000000.0 / freq);
			float t_inter = (float) (data.time_inter * 1000000.0 / freq);
			float t_slack = (float) (data.time_slack * 1000000.0 / freq);
			float t_dma = (float) (data.time_dma * 1000000.0 / freq);

			// Construimos el bloque completo usando snprintf
			snprintf(multi_line_buf, sizeof(multi_line_buf),
					"%u,%lu,%.2f,%.2f,%.2f,%.2f,%u,%lu\r\n",
					CORE_ID, real_tick, t_read, t_dma, t_inter, t_slack, error,
					checksum);

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

int init_dma_in() {
	XAxiDma_Config *CfgPtr;
	int Status;

	CfgPtr = XAxiDma_LookupConfig(DMA_IN_DEV_ID);
	if (!CfgPtr) {
//		xil_printf("No config found for %d\r\n", DMA_IN_DEV_ID);
		return XST_FAILURE;
	}

	Status = XAxiDma_CfgInitialize(&AxiDmaIn, CfgPtr);
	if (Status != XST_SUCCESS) {
//		xil_printf("Initialization failed %d\r\n", Status);
		return XST_FAILURE;
	}

	if (AxiDmaIn.HasMm2S) {
//		xil_printf("Canal MM2S detectado y listo.\r\n");
	} else {
//		xil_printf("Error: El hardware DMA no tiene el canal MM2S activo.\r\n");
		return XST_FAILURE;
	}

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

