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

// --- DMA de salida desde el PL, entrada al PS ---
#define DMA_OUT_DEV_ID XPAR_AXI_DMA_OUT_DEVICE_ID
#define DMA_IN_DEV_ID XPAR_AXI_DMA_IN_DEVICE_ID
#define DMA_OUT_IRQ_ID XPAR_FABRIC_AXI_DMA_OUT_S2MM_INTROUT_INTR
#define BRAM_IDA_ADDR      0x84000000
#define BRAM_OUT_ADDR      0x82000000
#define SAMPLES 8192
#define SAMPLES_DMA 524288
#define BUFFER_SIZE (SAMPLES * 2)
#define BUFFER_SIZE_DMA (SAMPLES_DMA * 2)

// --- CONFIGURACION ---
#define CORE_ID 0
#define PL_IRQ_ID 123U
#define INTC_DEVICE_ID XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TEST_COUNT 3 // Mediremos el tiempo que toman 10 IRQs
#define TEST_DATA  110

#define TEST_BRUTE 1000

#define TICK_10MS 1000000 // A 100MHz

// Buffer alineado para optimizar el uso de NEON con memcpy
u32 local_data_dma[SAMPLES_DMA] __attribute__((aligned(64)));
u32 local_data_ram[SAMPLES] __attribute__((aligned(64)));

// FIJAMOS EL TAMANO: 48000 muestras de 16 bits = 96000 bytes
const int FIXED_SAMPLES = SAMPLES_DMA;
const int BYTES_TO_TRANSFER = FIXED_SAMPLES * 4;

volatile uint32_t *bram_ptr = (volatile uint32_t *) BRAM_OUT_ADDR;

// Usamos la instancia que FreeRTOS ya crea internamente
extern XScuGic xInterruptController;
SemaphoreHandle_t xIrqSemaphore = NULL;
QueueHandle_t xLogQueue = NULL;
volatile uint32_t interrupt_counter = 0;
volatile uint64_t t_isr_timestamp;

// Variables de la DMA
// u32 RxBuffer[SAMPLES] __attribute__((aligned(64))); // Cambiar a u32
XAxiDma AxiDmaOut; // Instance of the DMA engine
XAxiDma AxiDmaIn;

typedef struct {
	uint32_t rd_clean_ns;  // Lectura BRAM sin DMA
	uint32_t rd_stress_ns; // Lectura BRAM con DMA corriendo
	uint32_t wr_clean_ns;  // Escritura BRAM sin DMA
	uint32_t wr_stress_ns; // Escritura BRAM con DMA corriendo
	uint32_t dma_time_ns;  // Cuánto tardó el DMA en su propia tarea
} TBenchmarkData;

// Functions to measure time
// --- HELPERS TIEMPO ARM A53 ---
static inline uint64_t get_hw_time(void) {
	uint64_t val;
	asm volatile("mrs %0, cntpct_el0" : "=r"(val));
	return val;
}

static inline uint32_t get_hw_freq(void) {
	uint32_t val;
	asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
	return val;
}

// Prototipo de funciones
int init_dma_out();
int init_dma_in();
int SetupInterruptSystem(XScuGic *GicInstPtr);

void vReadMemTask(void *pvParameters);
void vTaskMaster(void *pvParameters);
void vLoggerTask(void *pvParameters);

// --- CORE 0: Caso 1 ---
void run_case_1_brute_force();
void run_case_2_brute_force();

int main(void) {
	xil_printf("\r\nCore 0 alive Contention Test V_1_0\r\n");

	xIrqSemaphore = xSemaphoreCreateBinary();
	// IMPORTANTE: Crear la cola para 10 registros
	xLogQueue = xQueueCreate(20, sizeof(TBenchmarkData));

	// 2. Configurar su propia interrupcion (ID 121U para Core 0)
	// SetupInterruptSystem(&xInterruptController);

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

	while (1) {

		vTaskDelay(pdMS_TO_TICKS(100)); // Polling muy rapido pero eficiente
	}

	vTaskDelete(NULL);
}

void vReadMemTask(void *pvParameters) {

	// uint64_t t_last_irq = 0;
	//uint64_t t_current_irq = 0;
	// uint32_t freq = get_hw_freq();
	int Status;
	//int cont = 0;

	TBenchmarkData log_entry;

	Status = init_dma_out();
	if (Status != XST_SUCCESS) {
		xil_printf("DMA OUT Initialization Failed\r\n");
		vTaskDelete(NULL);
	}

	Status = init_dma_in();
	if (Status != XST_SUCCESS) {
		// xil_printf("DMA OUT Initialization Failed\r\n");
		vTaskDelete(NULL);
	}

	// 2. Configurar su propia interrupcion (ID 121U para Core 0)
	SetupInterruptSystem(&xInterruptController);

	xil_printf("\r\n\r\nStarting Case 0: CPU-Driven none; N = 32768 in Core 1\r\n");

	for (int i = 0; i < TEST_COUNT; i++) {
		// 1. Esperar Pulso del PL
		xSemaphoreTake(xIrqSemaphore, portMAX_DELAY);

		// --- PRUEBA DE CONTENCION (LECTURA Y ESCRITURA) ---

		asm volatile("nop");

//		// 1. BASELINE: Medicion en condiciones limpias
//		uint64_t t_i_rd_limpio = get_hw_time();
//		memcpy(local_data_ram, (void*) BRAM_IDA_ADDR, SAMPLES * sizeof(u32));
//		uint64_t t_f_rd_limpio = get_hw_time();
//
//		uint64_t t_i_wr_limpio = get_hw_time();
//		memcpy((void*) BRAM_OUT_ADDR, local_data_ram, SAMPLES * sizeof(u32));
//		Xil_DCacheFlushRange((INTPTR) BRAM_OUT_ADDR, SAMPLES * sizeof(u32));
//		uint64_t t_f_wr_limpio = get_hw_time();
//
//		// 2. STRESS TEST: El DMA y el CPU compiten por el bus
//		Xil_DCacheFlushRange((INTPTR) local_data_dma, BYTES_TO_TRANSFER);
//
//		uint64_t t_start_dma_real = get_hw_time(); // Inicia reloj global de la transferencia
//
//		Status = XAxiDma_SimpleTransfer(&AxiDmaOut, (UINTPTR) local_data_dma,
//				BYTES_TO_TRANSFER, XAXIDMA_DEVICE_TO_DMA);
//
//		// Medicion de lectura mientras el DMA esta activo
//		uint64_t t_i_rd_stress = get_hw_time();
//		memcpy(local_data_ram, (void*) BRAM_IDA_ADDR, SAMPLES * sizeof(u32));
//		uint64_t t_f_rd_stress = get_hw_time();
//
//		// Medicion de escritura mientras el DMA esta activo
//		uint64_t t_i_wr_stress = get_hw_time();
//		memcpy((void*) BRAM_OUT_ADDR, local_data_ram, SAMPLES * sizeof(u32));
//		Xil_DCacheFlushRange((INTPTR) BRAM_OUT_ADDR, SAMPLES * sizeof(u32));
//		uint64_t t_f_wr_stress = get_hw_time();
//
//		// 3. ESPERAR CIERRE: Aseguramos que el DMA termino para calcular su tiempo total
//		int timeout = 1000000;
//		while (XAxiDma_Busy(&AxiDmaOut, XAXIDMA_DEVICE_TO_DMA) && timeout > 0) {
//			timeout--;
//		}
//		uint64_t t_end_dma_real = get_hw_time(); // Fin del tiempo de ocupacion real del bus
//
//		// 4. PREPARAR LOG (Diferencia de ticks)
//		log_entry.rd_clean_ns = (uint32_t) (t_f_rd_limpio - t_i_rd_limpio);
//		log_entry.rd_stress_ns = (uint32_t) (t_f_rd_stress - t_i_rd_stress);
//		log_entry.wr_clean_ns = (uint32_t) (t_f_wr_limpio - t_i_wr_limpio);
//		log_entry.wr_stress_ns = (uint32_t) (t_f_wr_stress - t_i_wr_stress);
//		log_entry.dma_time_ns = (uint32_t) (t_end_dma_real - t_start_dma_real);
//
//		xQueueSend(xLogQueue, &log_entry, 0);

//		//--------------------------------------------------
//		// Lectura del DMA
//		//--------------------------------------------------
//		Xil_DCacheFlushRange((INTPTR) local_data_dma, BYTES_TO_TRANSFER);
//
//		uint64_t t_start_read_dma = get_hw_time();
//
//		Status = XAxiDma_SimpleTransfer(&AxiDmaOut, (UINTPTR) local_data_dma,
//				BYTES_TO_TRANSFER, XAXIDMA_DEVICE_TO_DMA);
//
//		// ¡ESTO ES VITAL! Esperar a que el hardware termine de saturar el bus
//		while (XAxiDma_Busy(&AxiDmaOut, XAXIDMA_DEVICE_TO_DMA)) {
//			asm volatile("yield");
//		}
//
//		uint64_t t_end_read_dma = get_hw_time();
//		log_entry.t_read_dma = (uint16_t) (t_end_read_dma - t_start_read_dma);
//
//		// 5. Modificacion de datos (Inyectar el valor 123 para el monitor)
//		if (cont % 3 == 1) {
//			local_data_dma[100] = 123;
//		}
//		Xil_DCacheFlushRange((INTPTR) local_data_dma, BYTES_TO_TRANSFER);
//
//		Status = XAxiDma_SimpleTransfer(&AxiDmaIn, (UINTPTR) local_data_dma,
//				BYTES_TO_TRANSFER, XAXIDMA_DMA_TO_DEVICE);
//
//		xil_printf("\r\nDMA Transfer Complete. Data:\r\n");
//		for (int i = 0; i < TEST_DATA; i++) {
//			xil_printf("%d, ", local_data_dma[i]);
//		}
//
//		// 3. Copia masiva BRAM -> RAM (Muy rapido con memcpy)
//		uint64_t t_i_start_ram = get_hw_time();
//		Xil_DCacheInvalidateRange((INTPTR) BRAM_IDA_ADDR,
//		SAMPLES * sizeof(u32));
//		memcpy(local_data_ram, (void*) BRAM_IDA_ADDR, SAMPLES * sizeof(u32));
//		uint64_t t_i_end_ram = get_hw_time();
//
//		log_entry.t_read_ram = t_i_end_ram - t_i_start_ram;
//
//		// 5. Modificacion de datos (Inyectar el valor 123 para el monitor)
//		if (cont % 3 == 1) {
//		local_data_ram[25] = 123;
//		}
//		Xil_DCacheFlushRange((INTPTR)local_data_ram, SAMPLES * sizeof(u32));
//		memcpy((void*) BRAM_OUT_ADDR, local_data_ram, SAMPLES * sizeof(u32));
//		Xil_DCacheFlushRange((INTPTR)BRAM_OUT_ADDR, SAMPLES * sizeof(u32));
//
//		xil_printf("\r\nRAM Transfer Complete. Data:\r\n");
//		for (int i = 0; i < TEST_DATA; i++) {
//			xil_printf("%d, ", local_data_ram[i]);
//		}
//
//		xQueueSend(xLogQueue, &log_entry, 0);
//
//		cont++;

		// vTaskDelay(pdMS_TO_TICKS(2));
	}

	run_case_1_brute_force();
	run_case_2_brute_force();

	xil_printf("\r\n\r\nFinish Core 0: CPU-Driven none\r\n");
	// En lugar de borrar la tarea
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	vTaskDelete(NULL);
}

// --- CORE 0: Caso 1 ---
void run_case_1_brute_force() {
	xil_printf("\r\n\r\nStarting Case 1: CPU-Driven BRAM to RAM; N = 8192 read and write in BRAM\r\n");
	for (int i = 0; i < 5 * TEST_BRUTE; i++) {
		// El CPU mueve datos, ocupando su puerto maestro AXI
		memcpy(local_data_ram, (void*) BRAM_IDA_ADDR, SAMPLES * 4);
		Xil_DCacheFlushRange((INTPTR) local_data_ram, SAMPLES * 4);

		local_data_ram[25] = 123;

		Xil_DCacheFlushRange((INTPTR) local_data_ram, SAMPLES * sizeof(u32));
		memcpy((void*) BRAM_OUT_ADDR, local_data_ram, SAMPLES * sizeof(u32));
		Xil_DCacheFlushRange((INTPTR) BRAM_OUT_ADDR, SAMPLES * sizeof(u32));
	}
}

// --- CORE 0: Caso 2 ---
void run_case_2_brute_force() {
	xil_printf("\r\n\r\nStarting Case 2: DMA-Driven PL to RAM; N = 524288 read and write in DMA\r\n");
	for (int i = 0; i < TEST_BRUTE; i++) {
		// Lanzamos el DMA de 2MB que ya probamos
		XAxiDma_SimpleTransfer(&AxiDmaOut, (UINTPTR) local_data_dma,
				BYTES_TO_TRANSFER, XAXIDMA_DEVICE_TO_DMA);

		while (XAxiDma_Busy(&AxiDmaOut, XAXIDMA_DEVICE_TO_DMA)) {
			asm volatile("nop");
		}

		local_data_dma[100] = 123;
		Xil_DCacheFlushRange((INTPTR) local_data_dma, BYTES_TO_TRANSFER);

		XAxiDma_SimpleTransfer(&AxiDmaIn, (UINTPTR) local_data_dma,
				BYTES_TO_TRANSFER, XAXIDMA_DMA_TO_DEVICE);

		while (XAxiDma_Busy(&AxiDmaOut, XAXIDMA_DEVICE_TO_DMA)) {
			asm volatile("nop");
		}
	}
}

void vLoggerTask(void *pvParameters) {
	TBenchmarkData data;

	xil_printf("\r\n--- START OF LOG ---\r\n");
	xil_printf("DATA_START\r\n");
	// Encabezado para el CSV
	xil_printf("rd_clean,rd_stress,wr_clean,wr_stress,dma_time\r\n");

	while (1) {
		if (xQueueReceive(xLogQueue, &data, portMAX_DELAY) == pdPASS) {
			// Imprimimos directamente los valores separados por comas
			xil_printf("%u,%u,%u,%u,%u\r\n", data.rd_clean_ns,
					data.rd_stress_ns, data.wr_clean_ns, data.wr_stress_ns,
					data.dma_time_ns);
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
	if (interrupt_counter % 300 == 0) {
		xSemaphoreGiveFromISR(xIrqSemaphore, &xHigherPriorityTaskWoken);
	}

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

// --- NUEVO: DIRECCIONAMIENTO AL CORE ESPECIFICO ---
// Esto asegura que la interrupcion 122 vaya al Core 1, etc.
	u32 CpuMask = (1 << CORE_ID);
	u32 TargetReg = XScuGic_DistReadReg(GicInstPtr,
			XSCUGIC_SPI_TARGET_OFFSET_CALC(PL_IRQ_ID));
	TargetReg = (TargetReg & ~0xFF) | CpuMask;
	XScuGic_DistWriteReg(GicInstPtr, XSCUGIC_SPI_TARGET_OFFSET_CALC(PL_IRQ_ID),
			TargetReg);

	XScuGic_Enable(GicInstPtr, PL_IRQ_ID);
	return XST_SUCCESS;
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

// Leemos el registro de estado especifico de S2MM (Entrada)
// Usamos el offset 0x34 directamente para no fallar
	u32 s2mm_status = XAxiDma_ReadReg(CfgPtr->BaseAddr, 0x34);
	xil_printf("S2MM Status Register (Entrada): 0x%08X\r\n", s2mm_status);

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
