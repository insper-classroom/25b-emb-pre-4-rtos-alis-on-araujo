/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

// --- Pinos (ajuste conforme seu kit) ---
const uint BTN_PIN_R = 28;   // botão "vermelho"
const uint BTN_PIN_Y = 21;   // botão "amarelo"
const uint LED_PIN_R = 5;    // LED vermelho
const uint LED_PIN_Y = 10;   // LED amarelo

// --- RTOS handles ---
static QueueHandle_t     xQueueBtn;        // fila para IDs de botões
static SemaphoreHandle_t xSemaphoreLedR;   // sinal p/ LED R
static SemaphoreHandle_t xSemaphoreLedY;   // sinal p/ LED Y

// --- Callback de interrupção dos botões ---
// Envia 'R' ou 'Y' para a fila, SEM bloquear, usando a API de ISR.
void btn_callback(uint gpio, uint32_t events) {
    if ((events & GPIO_IRQ_EDGE_FALL) == 0) return; // ativa na borda de descida

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t id = 0;

    if (gpio == BTN_PIN_R) id = 'R';
    else if (gpio == BTN_PIN_Y) id = 'Y';
    else return;

    xQueueSendFromISR(xQueueBtn, &id, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// --- Task que despacha eventos de botão para os semáforos de LED ---
void btn_task(void *p) {
    uint8_t id;
    for (;;) {
        if (xQueueReceive(xQueueBtn, &id, portMAX_DELAY) == pdTRUE) {
            if (id == 'R') {
                xSemaphoreGive(xSemaphoreLedR);
            } else if (id == 'Y') {
                xSemaphoreGive(xSemaphoreLedY);
            }
        }
    }
}

// --- Task genérica para LED (pisca a cada 100 ms quando ativa) ---
static void led_task(uint led_pin, SemaphoreHandle_t sem) {
    gpio_init(led_pin);
    gpio_set_dir(led_pin, GPIO_OUT);
    gpio_put(led_pin, 0);

    bool ativo = false;
    bool level = false;

    for (;;) {
        // Espera até 100 ms por um "give" do semáforo.
        // Se vier um sinal, alterna estado (liga/desliga o pisca).
        if (xSemaphoreTake(sem, pdMS_TO_TICKS(100)) == pdTRUE) {
            ativo = !ativo;
            if (!ativo) {
                level = false;
                gpio_put(led_pin, 0); // garante apagado ao desativar
            }
        } else {
            // Timeout de 100 ms: é o "relógio" do pisca
            if (ativo) {
                level = !level;
                gpio_put(led_pin, level);
            }
        }
    }
}

void led_r_task(void *p) { led_task(LED_PIN_R, xSemaphoreLedR); }
void led_y_task(void *p) { led_task(LED_PIN_Y, xSemaphoreLedY); }

// --- Inicialização dos botões com pull-up e IRQ em borda de descida ---
static void btn_init(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_FALL, true);
}

int main() {
    stdio_init_all();

    // Cria fila e semáforos (binários)
    xQueueBtn      = xQueueCreate(16, sizeof(uint8_t));
    xSemaphoreLedR = xSemaphoreCreateBinary();
    xSemaphoreLedY = xSemaphoreCreateBinary();

    // Inicializa GPIOs de botão e registra callback uma única vez
    btn_init(BTN_PIN_R);
    btn_init(BTN_PIN_Y);
    gpio_set_irq_enabled_with_callback(BTN_PIN_R, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled(BTN_PIN_Y, GPIO_IRQ_EDGE_FALL, true); // já há callback registrado

    // Cria tasks
    xTaskCreate(btn_task,   "BTN",   256, NULL, 2, NULL);
    xTaskCreate(led_r_task, "LED_R", 256, NULL, 1, NULL);
    xTaskCreate(led_y_task, "LED_Y", 256, NULL, 1, NULL);

    vTaskStartScheduler();
    while (true) { /* nunca deve chegar aqui */ }
}
