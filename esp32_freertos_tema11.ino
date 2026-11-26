#include <Arduino.h>
#include <DHT.h>

// =====================
// MAPEAMENTO DE PINOS
// =====================

#define PINO_DHT        15
#define PINO_BOTAO      13
#define PINO_LED_VERDE  12
#define PINO_LED_VERM   14

#define TIPO_DHT        DHT11
DHT dht(PINO_DHT, TIPO_DHT);

// ==========================================
// ESTRUTURAS E OBJETOS DO FREERTOS
// ==========================================

typedef struct {
  float temperatura;
  float umidade;
  uint32_t leituraId;
} DadosSensor_t;

QueueHandle_t filaSensor;             // Fila para comunicação Produtor -> Consumidor
SemaphoreHandle_t semaforoEmergencia; // Semáforo binário disparado pela ISR do botão
SemaphoreHandle_t mutexSerial;        // Mutex para evitar mistura de texto na Serial

// Debounce via software para o botão físico
volatile uint32_t ultimoTempoInterrupcao = 0;

// ==========================================
// ROTINA DE INTERRUPÇÃO (ISR) DO BOTÃO
// ==========================================
void IRAM_ATTR isrBotaoEmergencia() {
  uint32_t tempoAtual = millis();
  if (tempoAtual - ultimoTempoInterrupcao > 250) { // Debounce de 250 ms
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Libera o semáforo para acordar a Task de Emergência
    xSemaphoreGiveFromISR(semaforoEmergencia, &xHigherPriorityTaskWoken);
    ultimoTempoInterrupcao = tempoAtual;

    // Força troca de contexto imediata (Preempção) se a task acordada tiver maior prioridade
    if (xHigherPriorityTaskWoken == pdTRUE) {
      portYIELD_FROM_ISR();
    }
  }
}

// Função auxiliar segura para imprimir no Monitor Serial
void printSeguro(const String &mensagem) {
  if (xSemaphoreTake(mutexSerial, portMAX_DELAY) == pdTRUE) {
    Serial.println(mensagem);
    xSemaphoreGive(mutexSerial);
  }
}

// ==========================================
// TASK 1: PRODUTORA (Prioridade 1 - Baixa)
// Lê o DHT11 e envia para a Fila
// ==========================================
void taskProdutora(void *pvParameters) {
  DadosSensor_t pacote;
  uint32_t contador = 0;

  for (;;) {
    float temp = dht.readTemperature();
    float umid = dht.readHumidity();

    if (isnan(temp) || isnan(umid)) {
      printSeguro("[Task Produtora - Prio 1] Falha ao ler DHT11! Verifique os fios do pino 15.");
    } else {
      contador++;
      pacote.temperatura = temp;
      pacote.umidade = umid;
      pacote.leituraId = contador;

      // Envia a struct para a fila (aguarda até 100ms se estiver cheia)
      if (xQueueSend(filaSensor, &pacote, pdMS_TO_TICKS(100)) == pdPASS) {
        // Pisca o LED Verde indicando aquisição normal
        digitalWrite(PINO_LED_VERDE, HIGH);
        vTaskDelay(pdMS_TO_TICKS(200));
        digitalWrite(PINO_LED_VERDE, LOW);
      }
    }

    // Aguarda 2 segundos antes da próxima leitura (libera a CPU)
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ==========================================
// TASK 2: CONSUMIDORA (Prioridade 2 - Média)
// Recebe da Fila e processa os dados
// ==========================================
void taskConsumidora(void *pvParameters) {
  DadosSensor_t dadoRecebido;

  for (;;) {
    // Fica bloqueada (sem gastar CPU) até chegar um item na fila
    if (xQueueReceive(filaSensor, &dadoRecebido, portMAX_DELAY) == pdPASS) {
      String logMsg = "[Task Consumidora - Prio 2] Leitura #" + String(dadoRecebido.leituraId) +
                      " | Temp: " + String(dadoRecebido.temperatura, 1) + " C" +
                      " | Umid: " + String(dadoRecebido.umidade, 1) + " %";
      printSeguro(logMsg);
    }
  }
}

// ==========================================
// TASK 3: EMERGÊNCIA (Prioridade 3 - Alta)
// Preempta qualquer tarefa ao apertar o botão
// ==========================================
void taskEmergencia(void *pvParameters) {
  for (;;) {
    // Fica bloqueada aguardando o semáforo da interrupção do botão (D13)
    if (xSemaphoreTake(semaforoEmergencia, portMAX_DELAY) == pdTRUE) {
      digitalWrite(PINO_LED_VERDE, LOW);
      digitalWrite(PINO_LED_VERM, HIGH);

      printSeguro("\n>>> [ALERTA - TASK EMERGENCIA - PRIO 3] BOTAO ACIONADO! PREEMPÇÃO EXECUTADA! <<<\n");

      // Mantém o LED Vermelho aceso por 1,5 segundo simulando tratamento crítico
      vTaskDelay(pdMS_TO_TICKS(1500));
      digitalWrite(PINO_LED_VERM, LOW);
    }
  }
}

// ==========================================
// SETUP: INICIALIZAÇÃO DO HARDWARE E RTOS
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_VERM, OUTPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP); // Resistor pull-up interno ativado

  digitalWrite(PINO_LED_VERDE, LOW);
  digitalWrite(PINO_LED_VERM, LOW);

  dht.begin();

  // Criação dos mecanismos de sincronização e IPC do FreeRTOS
  filaSensor = xQueueCreate(5, sizeof(DadosSensor_t));
  semaforoEmergencia = xSemaphoreCreateBinary();
  mutexSerial = xSemaphoreCreateMutex();

  // Configura interrupção na borda de descida (HIGH -> LOW ao apertar o botão)
  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO), isrBotaoEmergencia, FALLING);

  // Criação das 3 Tasks com prioridades crescentes (1 < 2 < 3)
  xTaskCreate(taskProdutora,   "Produtora",   4096, NULL, 1, NULL);
  xTaskCreate(taskConsumidora, "Consumidora", 4096, NULL, 2, NULL);
  xTaskCreate(taskEmergencia,  "Emergencia",  4096, NULL, 3, NULL);

  printSeguro("=== Sistema FreeRTOS Iniciado (115200 baud) ===");
}

void loop() {
  // No FreeRTOS, o loop padrão do Arduino não é utilizado; deletamos a task dele
  vTaskDelete(NULL);
}