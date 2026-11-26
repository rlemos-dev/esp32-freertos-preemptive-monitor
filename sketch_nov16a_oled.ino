#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =====================
// MAPEAMENTO DE PINOS
// =====================

#define PINO_DHT        15
#define PINO_BOTAO      13
#define PINO_LED_VERDE  12
#define PINO_LED_VERM   14

// Display OLED I2C (SDA = GPIO 21, SCL = GPIO 22 - pinos padrão do ESP32)
#define LARGURA_OLED    128
#define ALTURA_OLED     64
#define ENDERECO_OLED   0x3C   // se não acender, teste 0x3D
Adafruit_SSD1306 oled(LARGURA_OLED, ALTURA_OLED, &Wire, -1);

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
SemaphoreHandle_t mutexDisplay;       // Mutex: só uma task por vez usa o barramento I2C

DadosSensor_t ultimoDado;             // Última leitura (para restaurar a tela após o alerta)
volatile bool temDado = false;
volatile bool emAlerta = false;       // Enquanto true, a Consumidora não sobrescreve a tela

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

// Mostra temperatura e umidade no OLED
void mostrarLeitura(const DadosSensor_t &d) {
  if (xSemaphoreTake(mutexDisplay, portMAX_DELAY) == pdTRUE) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print("Leitura #"); oled.print(d.leituraId);
    oled.setTextSize(2);
    oled.setCursor(0, 20);
    oled.print(d.temperatura, 1); oled.print(" C");
    oled.setCursor(0, 44);
    oled.print(d.umidade, 0); oled.print(" %");
    oled.display();
    xSemaphoreGive(mutexDisplay);
  }
}

// Mostra o alerta de emergência no OLED
void mostrarAlerta() {
  if (xSemaphoreTake(mutexDisplay, portMAX_DELAY) == pdTRUE) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(2);
    oled.setCursor(0, 8);
    oled.print("ALERTA!");
    oled.setTextSize(1);
    oled.setCursor(0, 36);
    oled.print("Botao acionado");
    oled.setCursor(0, 48);
    oled.print("Task prio 3 ativa");
    oled.display();
    xSemaphoreGive(mutexDisplay);
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

      ultimoDado = dadoRecebido;
      temDado = true;
      if (!emAlerta) mostrarLeitura(dadoRecebido);   // atualiza a tela
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
      emAlerta = true;
      mostrarAlerta();

      printSeguro("\n>>> [ALERTA - TASK EMERGENCIA - PRIO 3] BOTAO ACIONADO! PREEMPÇÃO EXECUTADA! <<<\n");

      // Mantém o LED Vermelho aceso por 1,5 segundo simulando tratamento crítico
      vTaskDelay(pdMS_TO_TICKS(1500));
      digitalWrite(PINO_LED_VERM, LOW);
      emAlerta = false;
      if (temDado) mostrarLeitura(ultimoDado);       // volta a mostrar a temperatura
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

  Wire.begin(21, 22);                                // SDA, SCL
  if (!oled.begin(SSD1306_SWITCHCAPVCC, ENDERECO_OLED)) {
    Serial.println("Falha ao iniciar o OLED! Verifique os fios e o endereco I2C.");
  } else {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print("FreeRTOS iniciado");
    oled.display();
  }

  // Criação dos mecanismos de sincronização e IPC do FreeRTOS
  filaSensor = xQueueCreate(5, sizeof(DadosSensor_t));
  semaforoEmergencia = xSemaphoreCreateBinary();
  mutexSerial = xSemaphoreCreateMutex();
  mutexDisplay = xSemaphoreCreateMutex();

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
