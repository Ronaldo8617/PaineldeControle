/*
 * Painel de Controle de Acesso
 * Desenvolvido com base no conhecimento de FreeRTOS, GPIO, I2C, PWM,
 * OLED SSD1306, Buzzer e Matriz de LEDs.
 * Por: Ronaldo César Santos Rocha
 * Data: 26/05/2025
 *
 * Descrição: Sistema embarcado para simular o controle de acesso de usuários a um espaço,
 * utilizando FreeRTOS para gerenciar tarefas e semáforos/mutexes para sincronização.
 * Oferece feedback visual via LED RGB, sinalização sonora com buzzer e
 * exibição de informações no display OLED.
 */

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "stdio.h"
#include "hardware/sync.h" // Necessário para irq_set_enabled
#include "hardware/irq.h"  // Necessário para IO_IRQ_GPIO_GROUP0
#include "pico/bootrom.h"  // Para reset_usb_boot

// FreeRTOS includes
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h" // Para semáforos e mutexes

// Libs customizadas
#include "lib/display_init.h" // Contém extern ssd, display(), etc.
#include "lib/font.h"         // Necessário para a fonte 
#include "lib/buzzer.h"      // Funções para controle do buzzer


// --- Definições de Hardware (Pinos) --- //
#define BOTAO_ENTRADA 5 // GPIO 5 para entrada de usuário
#define BOTAO_SAIDA   6 // GPIO 6 para saída de usuário
#define BOTAO_RESET   22 // GPIO 22 para reset do sistema (Joystick)

#define BUZZER_GPIO   buzzer  // Definido em lib/buzzer.h (GPIO 21)

#define LED_R_GPIO    13      // PWM vermelho
#define LED_G_GPIO    11      // PWM verde
#define LED_B_GPIO    12      // PWM azul

// --- Configuração do Display OLED --- //
#define I2C_PORT      i2c1
#define I2C_SDA_PIN   14
#define I2C_SCL_PIN   15
#define OLED_ADDR     0x3C
#define OLED_WIDTH    128
#define OLED_HEIGHT   64

// --- Variáveis Globais e Handles do FreeRTOS --- //
SemaphoreHandle_t xDisplayMutex;   // Mutex para proteger o acesso ao display
SemaphoreHandle_t xUsuariosSem;    // Semáforo de contagem para usuários ativos
SemaphoreHandle_t xResetSem;       // Semáforo binário para o evento de reset
SemaphoreHandle_t xEntradaSem;     // Semáforo binário para evento de entrada
SemaphoreHandle_t xSaidaSem;       // Semáforo binário para evento de saída

// Variável para a contagem de usuários ativos
volatile uint8_t g_num_usuarios_ativos = 0;
// Variável para a capacidade máxima de usuários
const uint8_t MAX_USUARIOS = 9; // Exemplo: Capacidade máxima de 8 usuários

// --- Variáveis para Debounce --- //
volatile uint32_t last_debounce_time_entrada = 0;
volatile uint32_t last_debounce_time_saida = 0;
volatile uint32_t last_debounce_time_reset = 0;
#define DEBOUNCE_DELAY_MS 200 // Tempo de debounce em milissegundos

// --- Prototipos das Funções de Tarefas --- //
void vTaskEntrada(void *pvParameters);  // Tarefa de entrada
void vTaskSaida(void *pvParameters);    // Tarefa de saída
void vTaskReset(void *pvParameters);    // Tarefa de reset

// --- Funções de Feedback (Auxiliares) ---
void atualizar_feedback_display(void);
void atualizar_feedback_led_rgb(void);

// --- ÚNICA FUNÇÃO DE CALLBACK DE INTERRUPÇÃO GLOBAL (gpio_irq_handler) ---
void gpio_irq_handler(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());

    // Ações para o Botão de ENTRADA (BOTAO_ENTRADA)
    if (gpio == BOTAO_ENTRADA && (current_time_ms - last_debounce_time_entrada > DEBOUNCE_DELAY_MS)) {
        last_debounce_time_entrada = current_time_ms;
        xSemaphoreGiveFromISR(xEntradaSem, &xHigherPriorityTaskWoken); // Sinaliza a tarefa vTaskEntrada
    }
    // Ações para o Botão de SAÍDA (BOTAO_SAIDA)
    else if (gpio == BOTAO_SAIDA && (current_time_ms - last_debounce_time_saida > DEBOUNCE_DELAY_MS)) {
        last_debounce_time_saida = current_time_ms;
        xSemaphoreGiveFromISR(xSaidaSem, &xHigherPriorityTaskWoken);   // Sinaliza a tarefa vTaskSaida
    }
    // Ações para o Botão de RESET (BOTAO_RESET)
    else if (gpio == BOTAO_RESET && (current_time_ms - last_debounce_time_reset > DEBOUNCE_DELAY_MS)) {
        last_debounce_time_reset = current_time_ms;
        xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);   // Sinaliza a tarefa vTaskReset
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// --- Funções de Inicialização de Hardware --- //
void init_rgb_leds() {
    gpio_set_function(LED_R_GPIO, GPIO_FUNC_PWM);
    gpio_set_function(LED_G_GPIO, GPIO_FUNC_PWM);
    gpio_set_function(LED_B_GPIO, GPIO_FUNC_PWM);

    uint slice_r = pwm_gpio_to_slice_num(LED_R_GPIO);
    uint slice_g = pwm_gpio_to_slice_num(LED_G_GPIO);
    uint slice_b = pwm_gpio_to_slice_num(LED_B_GPIO);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, 255);

    pwm_init(slice_r, &config, true);
    pwm_init(slice_g, &config, true);
    pwm_init(slice_b, &config, true);

    pwm_set_gpio_level(LED_R_GPIO, 0);
    pwm_set_gpio_level(LED_G_GPIO, 0);
    pwm_set_gpio_level(LED_B_GPIO, 0);
}

void set_rgb_color(uint8_t r, uint8_t g, uint8_t b) {
    pwm_set_gpio_level(LED_R_GPIO, r);
    pwm_set_gpio_level(LED_G_GPIO, g);
    pwm_set_gpio_level(LED_B_GPIO, b);
}

// --- Funções de Feedback (Auxiliares) ---
// Função para atualizar o display OLED
void atualizar_feedback_display(void) {
    char buffer[32];
    if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
        ssd1306_fill(&ssd, false);

        snprintf(buffer, sizeof(buffer), "Users: %d/%d", g_num_usuarios_ativos, MAX_USUARIOS);
        ssd1306_draw_string(&ssd, buffer, 0, 0);

        if (g_num_usuarios_ativos == 0) {
            ssd1306_draw_string(&ssd, "STATUS: VACANT", 0, 20);
        } else if (g_num_usuarios_ativos < MAX_USUARIOS) {
            ssd1306_draw_string(&ssd, "STATUS: OK", 0, 20);
        } else {
            ssd1306_draw_string(&ssd, "STATUS: FULL!!!", 0, 20);
        }

        ssd1306_send_data(&ssd);
        xSemaphoreGive(xDisplayMutex);
    }
}

// Função para atualizar o LED RGB
void atualizar_feedback_led_rgb(void) {
    if (g_num_usuarios_ativos == 0) {
        set_rgb_color(0, 0, 255); // Azul - Nenhum usuário logado
    } else if (g_num_usuarios_ativos > 0 && g_num_usuarios_ativos <= (MAX_USUARIOS - 2)) {
        set_rgb_color(0, 255, 0); // Verde - Usuários ativos (de 0 a MAX-2)
    } else if (g_num_usuarios_ativos == (MAX_USUARIOS - 1)) {
        set_rgb_color(255, 255, 0); // Amarelo - Apenas 1 vaga restante
    } else { // g_num_usuarios_ativos == MAX_USUARIOS
        set_rgb_color(255, 0, 0); // Vermelho - Capacidade máxima
    }
}


// --- Tarefas FreeRTOS --- //

// Tarefa 1: Entrada de usuário
void vTaskEntrada(void *pvParameters) {
    (void) pvParameters;
    uint8_t prev_num_usuarios_for_beep = 0; // Para beep de "cheio"

    for (;;) {
        // Espera pelo semáforo binário de entrada, sinalizado pela ISR
        if (xSemaphoreTake(xEntradaSem, portMAX_DELAY) == pdTRUE) {
            prev_num_usuarios_for_beep = g_num_usuarios_ativos; // Salva o estado antes da tentativa

            // Tenta aumentar o número de usuários (dando um token ao xUsuariosSem)
            BaseType_t result = xSemaphoreGive(xUsuariosSem);
            g_num_usuarios_ativos = (uint8_t)uxSemaphoreGetCount(xUsuariosSem); // Atualiza global para feedback

            // Lógica de feedback: Aviso e beep se o limite for atingido
            if (result == pdFAIL && g_num_usuarios_ativos == MAX_USUARIOS) {
                // Se a tentativa de entrada falhou porque o semáforo estava cheio (result == pdFAIL)
                // e a contagem de usuários já é o máximo, emite o beep de sistema cheio.
                buzzer_set_freq(BUZZER_GPIO, 500); // Tom de aviso
                sleep_ms(100);
                buzzer_stop(BUZZER_GPIO);
            }
            
            // Atualiza o feedback visual e de display após a ação de entrada
            atualizar_feedback_led_rgb();
            atualizar_feedback_display();
        }
    }
}

// Tarefa 2: Saída de usuário
void vTaskSaida(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        // Espera pelo semáforo binário de saída, sinalizado pela ISR
        if (xSemaphoreTake(xSaidaSem, portMAX_DELAY) == pdTRUE) {
            // Tenta reduzir o número de usuários (pegando um token do xUsuariosSem)
            // Se o semáforo de contagem estiver em zero, xSemaphoreTake(xUsuariosSem, 0)
            // falhará e não decrementará a contagem.
            xSemaphoreTake(xUsuariosSem, 0); // Tenta pegar um token, sem bloquear
            g_num_usuarios_ativos = (uint8_t)uxSemaphoreGetCount(xUsuariosSem); // Atualiza global

            // Atualiza o feedback visual e de display após a ação de saída
            atualizar_feedback_led_rgb();
            atualizar_feedback_display();
        }
    }
}

// Tarefa 3: Reset do sistema
void vTaskReset(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        // Espera pelo sinal do semáforo binário de reset
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE) {
            // Zera a contagem de usuários
            g_num_usuarios_ativos = 0;
            // Zera o semáforo de contagem: toma todos os tokens disponíveis.
            UBaseType_t current_count = uxSemaphoreGetCount(xUsuariosSem);
            while(current_count > 0) {
                xSemaphoreTake(xUsuariosSem, 0); // Tenta pegar sem bloquear
                current_count = uxSemaphoreGetCount(xUsuariosSem);
            }

            // Gera beep duplo conforme enunciado
            buzzer_set_freq(BUZZER_GPIO, 1500); // Tom alto
            sleep_ms(100);
            buzzer_stop(BUZZER_GPIO);
            sleep_ms(50); // Pequena pausa
            buzzer_set_freq(BUZZER_GPIO, 1500);
            sleep_ms(100);
            buzzer_stop(BUZZER_GPIO);

            // Atualiza o feedback visual e de display após o reset
            atualizar_feedback_led_rgb();
            atualizar_feedback_display();

            // Pequeno delay para evitar resets múltiplos muito rápidos
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}


// --- Função Principal --- //
int main() {
    stdio_init_all();

    // --- Configuração dos pinos dos botões ---
    // Botão ENTRADA (A)
    gpio_init(BOTAO_ENTRADA);
    gpio_set_dir(BOTAO_ENTRADA, GPIO_IN);
    gpio_pull_up(BOTAO_ENTRADA);
    // Botão SAIDA (B)
    gpio_init(BOTAO_SAIDA);
    gpio_set_dir(BOTAO_SAIDA, GPIO_IN);
    gpio_pull_up(BOTAO_SAIDA);
    // Botão RESET (J - Joystick)
    gpio_init(BOTAO_RESET);
    gpio_set_dir(BOTAO_RESET, GPIO_IN);
    gpio_pull_up(BOTAO_RESET);

    // Registra a gpio_irq_handler como a função de callback para BOTAO_ENTRADA.
    // Todas as outras interrupções GPIO também serão direcionadas para esta função.
    gpio_set_irq_enabled_with_callback(BOTAO_ENTRADA, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Habilita as interrupções para os outros pinos, eles serão direcionados
    // para a mesma gpio_irq_handler registrada acima.
    gpio_set_irq_enabled(BOTAO_SAIDA, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BOTAO_RESET, GPIO_IRQ_EDGE_FALL, true);

    // Inicializa I2C para o display
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // Inicializa o display OLED global
    display();

    // Inicializa os LEDs RGB
    init_rgb_leds();

    // Inicializa o Buzzer
    buzzer_init(BUZZER_GPIO, 1000);
    buzzer_stop(BUZZER_GPIO);

    // --- Criação de Semáforos e Mutexes --- //
    xUsuariosSem = xSemaphoreCreateCounting(MAX_USUARIOS, 0); // Capacidade, 0 usuários inicialmente
    xResetSem = xSemaphoreCreateBinary(); // Semáforo binário para sinalizar reset
    xEntradaSem = xSemaphoreCreateBinary(); // Semáforo binário para evento de entrada
    xSaidaSem = xSemaphoreCreateBinary();   // Semáforo binário para evento de saída
    xDisplayMutex = xSemaphoreCreateMutex(); // Mutex para proteger o display

    // --- Criação de Tarefas FreeRTOS --- //
    xTaskCreate(vTaskEntrada, "Entrada", configMINIMAL_STACK_SIZE + 256, NULL, 3, NULL);  // Tarefa de entrada
    xTaskCreate(vTaskSaida, "Saida", configMINIMAL_STACK_SIZE + 256, NULL, 3, NULL);      // Tarefa de saída
    xTaskCreate(vTaskReset, "Reset", configMINIMAL_STACK_SIZE + 256, NULL, 4, NULL);      // Tarefa de reset (maior prioridade para reset rápido)
   

    // Garante que o feedback inicial esteja correto (todos vagos)
    g_num_usuarios_ativos = (uint8_t)uxSemaphoreGetCount(xUsuariosSem); // Deverá ser 0
    atualizar_feedback_led_rgb();
    atualizar_feedback_display();


    // --- Inicia o Escalador FreeRTOS --- //
    vTaskStartScheduler();

    while (1) {
        tight_loop_contents();
    }
    return 0;
}