#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xscugic.h"
#include "xparameters.h"
#include "queue.h"
#include "fftw3.h"
#include <stdio.h>

#include "shared_mem.h"

// --- DMA de salida desde el PL, entrada al PS ---
#define SAMPLES 4096
#define BUFFER_SIZE (SAMPLES * 2)

// --- CONFIGURACION ---
#define CORE_ID            1
#define PL_IRQ_ID          124U
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

typedef struct {
	uint64_t real_tick;
	uint16_t error;
	uint32_t checksum;
	uint32_t time_read;
	uint32_t time_inter;
	uint32_t time_slack;
	uint32_t time_dma;
	uint16_t pkg;
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
void vLoggerTask(void *pvParameters);

int main(void) {
	// 1. Inicializar Ring Buffer
//	volatile uint8_t *pShared = (volatile uint8_t *) SHARED_MEM_BASE;
//	for (size_t i = 0; i < sizeof(shared_log_t); i++) {
//		pShared[i] = 0;
//	}

	xIrqSemaphore = xSemaphoreCreateBinary();
	// IMPORTANTE: Crear la cola para 10 registros
	xLogQueue = xQueueCreate(20, sizeof(TBenchmarkData));

//	// 2. Configurar su propia interrupciï¿½n (ID 121U para Core 0)
//	SetupInterruptSystem(&xInterruptController);

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
	int N = SAMPLES;
	size_t fft_buffer_size = sizeof(fftwf_complex) * (N / 2 + 1);
	log_entry.error = 0; // Resetear error en cada iteración
	log_entry.pkg = 0;

	// 1. Inicialización de FFTW
	fftwf_complex *in = (fftwf_complex*) fftwf_malloc(fft_buffer_size);
	fftwf_complex *master_data = (fftwf_complex*) fftwf_malloc(fft_buffer_size);
	float *out = (float*) fftwf_malloc(sizeof(float) * N);
	fftwf_plan p = fftwf_plan_dft_c2r_1d(N, in, out, FFTW_MEASURE);

	if (!in || !out) {
		xil_printf("Malloc error en Core 1\r\n");
		vTaskDelete(NULL);
	}

	const int BYTES_TO_TRANSFER = SAMPLES * 4; // 48000 * 4 bytes (float o uint32)

	// vTaskDelay(100);
	// 2. Avisar que estoy listo
	SYNC_BARRIER->core1_ready = 1;
	Xil_DCacheFlushRange((UINTPTR) SYNC_BARRIER, sizeof(sync_barrier_t));

	// 3. BLOQUEARSE hasta que el Core 0 de el "Go"
	while (!SYNC_BARRIER->system_go) {
		Xil_DCacheInvalidateRange((UINTPTR) SYNC_BARRIER,
				sizeof(sync_barrier_t));
		vTaskDelay(1);
	}

	// 2. Configurar su propia interrupcion (ID 121U para Core 0)
	SetupInterruptSystem(&xInterruptController);

	for (int i = 0; i < TEST_COUNT; i++) {
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
		// Fase de espera/lectura simulada (3ms)
		uint64_t t_start_read = get_hw_time();
//		uint64_t cycles_to_wait = (freq * 3) / 1000;
//		while (get_hw_time() < (t_start_read + cycles_to_wait)) {
//			vTaskDelay(pdMS_TO_TICKS(2));
//		}
		log_entry.real_tick = get_hw_time();
		log_entry.pkg += 1;
		log_entry.time_read = get_hw_time() - t_start_read;

		// --- ACCESO A PIPELINE (Entrada desde Core 0) ---
		uint32_t c1_idx = DATA_PIPELINE->fft_idx; // Usar el índice dedicado
		while (1) {
			Xil_DCacheInvalidateRange(
					(UINTPTR) &DATA_PIPELINE->packets[c1_idx].status, 32);
			if (DATA_PIPELINE->packets[c1_idx].status == BUF_READY_FOR_FFT)
				break;
			asm volatile("yield");
		}

		// Casteo correcto para indexar el payload
		uint32_t *src_payload =
				(uint32_t *) DATA_PIPELINE->packets[c1_idx].payload;

		// Coherencia: Invalidate antes de leer datos y checksum del Core 0
		Xil_DCacheInvalidateRange((UINTPTR) src_payload, BYTES_TO_TRANSFER);

		// Verificación de Checksum (Datos del Core 0)
		uint32_t calc_sum = src_payload[0] + src_payload[1] + src_payload[4092]
				+ src_payload[4093];
		if (calc_sum != DATA_PIPELINE->packets[c1_idx].checksum) {
			log_entry.error += 1;
		}

		// --- PROCESAMIENTO FFT ---
		uint32_t t_i_start = get_hw_time();

		// 2. Llenar el buffer maestro una sola vez (fuera del while)
		for (int i = 0; i < (N / 2 + 1); i++) {
			master_data[i][0] = (float) src_payload[i];
			master_data[i][1] = 0.0f;
		}

		memcpy(in, master_data, BYTES_TO_TRANSFER);

		fftwf_execute(p);

		// 5. Devolver el resultado al MISMO buffer (más eficiente)
		// memcpy(src_payload, out, BYTES_TO_TRANSFER);

		for (int j = 0; j < N; j++) {
			// Tomamos la magnitud o el valor real y lo escalamos si es necesario
			src_payload[j] = src_payload[j] + 10;
		}

//		// Solo pasa el dato y asegúrate de la coherencia:
//		Xil_DCacheInvalidateRange((UINTPTR)src_payload, BYTES_TO_TRANSFER);
//
//		// Forzamos un Flush manual antes de entregar al Core 3
//		Xil_DCacheFlushRange((UINTPTR)src_payload, BYTES_TO_TRANSFER);
//		__asm__ __volatile__ ("dmb sy" : : : "memory"); // Barrera de memoria total

		// 6. Nuevo Checksum del resultado
		uint32_t dat_sum = src_payload[0] + src_payload[1] + src_payload[4092]
				+ src_payload[4093];
		DATA_PIPELINE->packets[c1_idx].checksum = dat_sum;
		log_entry.checksum = dat_sum;

		// 7. Coherencia: Empujar a RAM y entregar al Core 3
		Xil_DCacheFlushRange((UINTPTR) src_payload, BYTES_TO_TRANSFER);
		__asm__ __volatile__ ("dmb sy" : : : "memory");

		// CAMBIO DE ESTADO: Ahora el Core 3 puede tomarlo
		DATA_PIPELINE->packets[c1_idx].status = BUF_READY_FOR_OUT;
		DATA_PIPELINE->fft_idx = (c1_idx + 1) % NUM_BUFFERS;

		log_entry.time_dma = get_hw_time() - t_i_start;
		// Guardar para el próximo cálculo
		t_last_irq = t_current_irq;
		xQueueSend(xLogQueue, &log_entry, 0);
		// vTaskDelay(pdMS_TO_TICKS(2));
	}

	fftwf_destroy_plan(p);
	fftwf_free(in);
	fftwf_free(out);

	// En lugar de borrar la tarea
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(1000));
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
			int pkg = data.pkg;
			long checksum = data.checksum;
			float t_read = (float) (data.time_read * 1000000.0 / freq);
			float t_inter = (float) (data.time_inter * 1000000.0 / freq);
			float t_slack = (float) (data.time_slack * 1000000.0 / freq);
			float t_dma = (float) (data.time_dma * 1000000.0 / freq);

			// Construimos el bloque completo usando snprintf
			snprintf(multi_line_buf, sizeof(multi_line_buf),
					"%u,%lu,%.2f,%.2f,%.2f,%.2f,%u,%lu,%u\r\n",
					CORE_ID, real_tick, t_read, t_dma, t_inter, t_slack, error,
					checksum, pkg);

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

