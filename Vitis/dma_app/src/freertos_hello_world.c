#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xgpio.h"
#include "xil_io.h"

// --- Definiciones Globales ---
#define BRAM_IDA_ADDR      0x82000000
#define SAMPLES            48000
#define PRINT_SAMPLES      1000

int vect_s[6] = { 2048, 4096, 8192, 16384, 32768, 48000 };

// Buffer alineado para optimizar el uso de NEON con memcpy
s16 local_data[SAMPLES] __attribute__ ((aligned (64)));

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

// --- Tarea de Impresión Serial ---
void vSerialPrintTask(void *pvParameters) {
	TickType_t xLastWakeTime;
	const TickType_t xFrequency = pdMS_TO_TICKS(10000); // 10 segundos

	uint64_t freq = get_hw_freq();

	xLastWakeTime = xTaskGetTickCount();
	xil_printf("Iniciando captura y lectura por memcpy...\r\n");

	// Prueba de escritura directa (bypass total de drivers)
	Xil_Out32(0x82000000, 12345);
	uint32_t val = Xil_In32(0x82000000);

	if (val == 12345) {
		xil_printf("¡El PS y la BRAM funcionan sin inicializar!\r\n");
	} else {
		xil_printf("ERROR: No se puede escribir en la BRAM.\r\n");
	}

	for (int i = 0; i < 6; i++) {

		// 3. Copia masiva BRAM -> RAM (Muy rápido con memcpy)
		uint64_t t_i_start = get_hw_time();
		memcpy(local_data, (void*) BRAM_IDA_ADDR, vect_s[i] * sizeof(s16));
		uint64_t t_i_end = get_hw_time() - t_i_start;

		float time = (float) (t_i_end * 1000000.0 / freq);

		// 4. Impresión Serial (Operación lenta, por eso usamos memcpy antes)
//		xil_printf("--- Datos del DDS (Primeros 1000) ---\r\n");
//		for (int i = 0; i < PRINT_SAMPLES; i++) {
//			xil_printf("%d, ", (long) local_data[i]);
//		}

		xil_printf("\r\nN = %d, read time = %d.%d (us), ticks = %lu \r\n",
				(int) vect_s[i], (int) time, (int) ((time - (int) time) * 100),
				t_i_end);

//        uint32_t *mem_ptr = (uint32_t*)BRAM_IDA_ADDR;
//        for(int i=0; i < 1000; i++) {
//            // Leemos el registro de 32 bits y extraemos los 16 bajos
//            int16_t sample = (int16_t)(mem_ptr[i] & 0xFFFF);
//            xil_printf("%d, ", sample);
//        }

		xil_printf("\r\n--- Fin de transmision ---\r\n");

		// Esperar hasta completar el ciclo de 10 segundos
		// vTaskDelayUntil(&xLastWakeTime, xFrequency);
		vTaskDelay(500);
	}

	vTaskDelete(NULL);
}

int main() {
	// 1. Deshabilitar interrupciones durante la inicialización
	vPortEnterCritical();

	xil_printf("\r\n--- Iniciando Sistema Kria AMP (BRAM + FreeRTOS) ---\r\n");

	// 3. Crear la Tarea de Impresion Serial
	// Prioridad tskIDLE_PRIORITY + 1 es suficiente para esta tarea
	BaseType_t xReturned = xTaskCreate(vSerialPrintTask, // Funcion que implementa la tarea
			"SerialTask",           // Nombre descriptivo
			2048,                 // Stack size (ajustado para xil_printf largo)
			NULL,                   // Parametros
			tskIDLE_PRIORITY + 1,   // Prioridad
			NULL                    // Handler
			);

	if (xReturned != pdPASS) {
		xil_printf("ERROR: No se pudo crear la tarea.\r\n");
		while (1)
			;
	}

	// 4. Salir de seccion critica
	vPortExitCritical();

	xil_printf("Lanzando Scheduler de FreeRTOS...\r\n");

	// 5. Iniciar el Scheduler
	vTaskStartScheduler();

	// El codigo nunca deberia llegar aqui
	for (;;)
		;
	return 0;
}
