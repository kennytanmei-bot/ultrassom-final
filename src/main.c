#include <zephyr/kernel.h>        // Kernel do Zephyr (k_msleep, k_busy_wait, etc.)
#include <zephyr/device.h>        // Acesso a dispositivos (GPIO, etc.)
#include <zephyr/drivers/gpio.h>  // API de GPIO
#include <pwm_z42.h>              // Biblioteca para configurar o TPM (timer)

/* ===== CONFIGURAÇÃO ===== */

/* TRIG */
#define TRIG_NODE DT_NODELABEL(gpioa) // Nó do Device Tree para GPIOA (onde está o TRIG)
#define TRIG_PIN  12                  // Pino que envia o pulso para o sensor (TRIG)

/* ⚠️ IMPORTANTE:
   Use um pino que realmente seja TPM1_CH0/CH1
   Exemplo comum na KL25Z:
   PTE20 = TPM1_CH0
*/

#define TPM_MODULE 1000 
#define m1 700
#define m2 750

#define ECHO_PORT GPIOE               // Porta onde chega o sinal ECHO
#define ECHO_PIN  20                  // Pino do ECHO (ligado ao TPM para captura)

#define TPM_IRQ_LINE TPM1_IRQn        // Linha de interrupção do TPM1
#define TPM_IRQ_PRIORITY 1            // Prioridade da interrupção

/* ===== VARIÁVEIS ===== */

volatile uint16_t t_start = 0;  // Armazena o tempo da borda de subida (início do pulso)
volatile uint16_t t_end = 0;    // Armazena o tempo da borda de descida (fim do pulso)
volatile uint8_t estado = 0;    // 0 = esperando subida, 1 = esperando descida
volatile uint8_t pronto = 0;    // Flag: 1 quando a medição terminou

const struct device *trig_dev;  // Ponteiro para o dispositivo GPIO do TRIG

/* ===== ISR (INTERRUPÇÃO DO TPM) ===== */

void tpm1_isr(void *arg)
{
    // Verifica se houve evento de captura no canal 0 do TPM
    if (TPM1->STATUS & TPM_STATUS_CH0F_MASK) {

        TPM1->STATUS |= TPM_STATUS_CH0F_MASK; // Limpa a flag de interrupção

        // Se estamos esperando a borda de subida (início do pulso ECHO)
        if (estado == 0) {
            t_start = TPM1->CONTROLS[0].CnV; // Salva o tempo atual do timer
            estado = 1;                      // Agora passamos a esperar a descida
        } else {
            // Captura da borda de descida (fim do pulso ECHO)
            t_end = TPM1->CONTROLS[0].CnV;

            pronto = 1; // Indica que já temos início e fim (medição completa)
            estado = 0; // Reseta o estado para próxima medição
        }

        printk("IRQ\n"); // Debug: confirma que houve captura
    }
}

/* ===== FUNÇÃO PARA GERAR O PULSO TRIG ===== */

void trigger(void)
{
    gpio_pin_set(trig_dev, TRIG_PIN, 0); // Garante nível baixo antes do pulso
    k_busy_wait(1);                      // Espera 2 µs (estabilização)

    gpio_pin_set(trig_dev, TRIG_PIN, 1); // Sobe o pino (início do pulso TRIG)
    k_busy_wait(5);                     // Mantém por 10 µs (requisito do sensor)

    gpio_pin_set(trig_dev, TRIG_PIN, 0); // Finaliza o pulso TRIG
}

/* ===== FUNÇÃO DE CÁLCULO DA DISTÂNCIA ===== */

float calcular(uint16_t a, uint16_t b)
{
    uint16_t delta;

    // Calcula a diferença de tempo entre fim e início (tratando overflow do timer)
    if (b >= a)
        delta = b - a;
    else
        delta = (65535 - a) + b;

    // Converte "ticks" do TPM para tempo em microsegundos
    // (2.67 µs por tick é uma aproximação para PS_128)
    float tempo_us = delta * 2.67f;

    // Fórmula do ultrassom:
    // distância = (velocidade do som * tempo) / 2
    float distancia = (tempo_us * 0.0343f) / 2.0f;

    return distancia; // Retorna distância em cm
}

/* ===== FUNÇÃO PRINCIPAL ===== */

int main(void)
{
    // Obtém o dispositivo GPIO do TRIG a partir do Device Tree
    trig_dev = DEVICE_DT_GET(TRIG_NODE);

    // Verifica se o dispositivo está pronto para uso
    if (!device_is_ready(trig_dev)) {
        printk("Erro TRIG\n");
        return -1;
    }

    // Configura o pino TRIG como saída
    gpio_pin_configure(trig_dev, TRIG_PIN, GPIO_OUTPUT);

    /* ===== CONFIGURA INTERRUPÇÃO ===== */

    IRQ_CONNECT(TPM_IRQ_LINE, TPM_IRQ_PRIORITY, tpm1_isr, NULL, 0); // Liga ISR ao TPM
    irq_enable(TPM_IRQ_LINE); // Habilita a interrupção

    /* ===== CONFIGURA TPM ===== */

    pwm_tpm_Init(TPM1, TPM_PLLFLL, 65535, TPM_CLK, PS_128, EDGE_PWM);
    // Inicializa o TPM1:
    // - clock vindo do PLL/FLL
    // - contador até 65535
    // - prescaler 128 (define velocidade do timer)

    /* ===== CONFIGURA CAPTURA ===== */

    // Canal 0: captura em ambas as bordas (subida e descida)
    pwm_tpm_Ch_Init(TPM1, 0,
        TPM_INPUT_CAPTURE_BOTH | TPM_CHANNEL_INTERRUPT,
        ECHO_PORT, ECHO_PIN);

    // Canal 1 (não essencial aqui, mas configurado no seu código)
    pwm_tpm_Ch_Init(TPM1, 1,
        TPM_INPUT_CAPTURE_FALLING | TPM_CHANNEL_INTERRUPT,
        ECHO_PORT, ECHO_PIN);

    printk("Sistema iniciado\n");

    pwm_tpm_Init(TPM2, TPM_PLLFLL, TPM_MODULE, TPM_CLK, PS_128, EDGE_PWM);
    pwm_tpm_Init(TPM0, TPM_PLLFLL, TPM_MODULE, TPM_CLK, PS_64, EDGE_PWM);


    pwm_tpm_Ch_Init(TPM0, 3, TPM_PWM_H, GPIOC, 4); // IN3 vermelho 
    pwm_tpm_Ch_Init(TPM0, 5, TPM_PWM_H, GPIOD, 5); // IN4 preto

    pwm_tpm_Ch_Init(TPM0, 1, TPM_PWM_H, GPIOA, 4); // IN2 branco
    pwm_tpm_Ch_Init(TPM0, 2, TPM_PWM_H, GPIOA, 5); // IN1 azul



    /* ===== LOOP PRINCIPAL ===== */
int re = 0;
    while (1)
    {
        pronto = 0; // Reseta flag de medição
        int contador = 0;

        trigger();  // Dispara o ultrassom

        /* espera ativa até capturar */
        int timeout = 1000000; // Limite para evitar travar para sempre

        // Espera até a ISR marcar pronto ou acabar o tempo
        while (!pronto && timeout--) {
            k_busy_wait(1); // Espera 1 µs por iteração
        }

        if (pronto) {
            printk("CAPTUROU!\n");
            printk("start: %u | end: %u\n", t_start, t_end);

            float d = calcular(t_start, t_end); // Calcula distância
            int d_int = (int)d;                 // Converte para inteiro (printk não usa float)

            printk("Distancia: %d cm\n", d_int);
            if(d>23.5999865){
            pwm_tpm_CnV(TPM0, 1,0); //tras esqeurdo 
            pwm_tpm_CnV(TPM0, 2,m1); //frente esquerdo
            pwm_tpm_CnV(TPM0, 3,m2); //frente direito
            pwm_tpm_CnV(TPM0, 5,0); //tras direito
            contador = 0;
            }
        
        else if(d<21.5999865){
                pwm_tpm_CnV(TPM0, 1,m1+50);
                pwm_tpm_CnV(TPM0, 2,0);
                pwm_tpm_CnV(TPM0, 3,0); 
                pwm_tpm_CnV(TPM0, 5,m2); 
               
                }
            if(d<23.5999865 && d>21.5999865){
                pwm_tpm_CnV(TPM0, 1,0);
                pwm_tpm_CnV(TPM0, 2,0);
                pwm_tpm_CnV(TPM0, 3,0); 
                pwm_tpm_CnV(TPM0, 5,0); 
            }

        }


         else {
            printk("Sem leitura\n"); // Não conseguiu medir
            printk("Timeout\n");
        }
    // Espera antes da próxima medição
    }

    return 0;
}