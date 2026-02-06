#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xil_cache.h"
#include <string.h>

// --- CONFIGURACION DE MEMORIA COMPARTIDA ---
// Esta direccion debe estar definida en tu diseño como compartida
#define SHARED_MEM_ADDR    0x70000000
#define BUFFER_SIZE_WORDS  32768

// Buffer local en la pila o seccion de datos del Core 1
uint32_t shared_buffer_local[BUFFER_SIZE_WORDS] __attribute__((aligned(64)));

// --- HELPERS TIEMPO ARM A53 (Igual que en Core 0) ---
static inline uint64_t get_hw_time(void) {
	uint64_t val;
	asm volatile("mrs %0, cntpct_el0" : "=r"(val));
	return val;
}

// --- TAREA DE MEDICION (TESTIGO) ---
// --- TAREA DE MEDICION (TESTIGO) ---
void vTaskCore1Metric(void *pvParameters) {
	xil_printf("\r\nCore 1: Task Metric Started (15 seconds test)\r\n");

	uint32_t iterations = 0;
	// 15 segundos / 10ms por iteracion = 1500 iteraciones
	const uint32_t max_iterations = 2000;

	while (iterations < max_iterations) {
		// Invalidamos cache para asegurar que leemos de DDR
		Xil_DCacheInvalidateRange((INTPTR) SHARED_MEM_ADDR,
				BUFFER_SIZE_WORDS * 4);

		uint64_t t_start = get_hw_time();

		// Operacion brute-force
		memcpy(shared_buffer_local, (void*) SHARED_MEM_ADDR,
				BUFFER_SIZE_WORDS * 4);

		uint64_t t_end = get_hw_time();
		uint32_t duration = (uint32_t) (t_end - t_start);

		// Imprimimos el tiempo
		xil_printf("%u, ", duration);

		iterations++;
		vTaskDelay(pdMS_TO_TICKS(10));
	}

	xil_printf("\r\n\r\n--- Core 1 Test Finished ---\r\n");

	// Matamos la tarea para que no haga nada mas
	vTaskDelete(NULL);
}

int main(void) {
	// No inicializamos perifericos que ya inicializo el Core 0 (como el UART o GIC)
	// a menos que sea estrictamente necesario para el Core 1.

	xil_printf("--- Core 1 Online (AMP Mode) ---\r\n");

	// Creamos la tarea con prioridad alta para que la medicion sea precisa
	xTaskCreate(vTaskCore1Metric, "Metric1", 2048,
	NULL,
	tskIDLE_PRIORITY + 4,
	NULL);

	// Arrancamos el scheduler del Core 1
	vTaskStartScheduler();

	while (1)
		;
	return 0;
}
