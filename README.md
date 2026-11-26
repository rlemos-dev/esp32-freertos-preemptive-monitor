# Monitoramento Ambiental e Resposta Crítica com FreeRTOS no ESP32

Prototipação de um **Sistema Operacional de Tempo Real (RTOS)** embarcado no microcontrolador **DOIT ESP32 DEVKIT V1**, desenvolvido para a **Avaliação 3 (Tema 11)** da Unidade Curricular de **Organização de Computadores e Sistemas Operacionais**.

## Objetivo do Projeto

Demonstrar na prática os fundamentos de sistemas operacionais de tempo real em comparação aos sistemas operacionais de propósito geral (GPOS), evidenciando:

* **Determinismo temporal** e baixa latência de resposta;

* **Escalonamento preemptivo baseado em prioridades fixas** ($1 < 2 < 3$);

* **Comunicação entre tarefas (IPC)** assíncrona via Fila (*Queue*);

* **Tratamento diferido de interrupções de hardware (ISR)** sincronizado por Semáforo Binário;

* **Exclusão mútua** no acesso ao barramento Serial (UART) via *Mutex*.

## Arquitetura Lógica (FreeRTOS)

O firmware divide a execução concorrente em três *tasks* independentes e uma rotina de interrupção externa:

| Prioridade | Tarefa / Rotina | Nível | Mecanismo RTOS | Função no Sistema | 
| ----- | ----- | ----- | ----- | ----- | 
| **Hardware** | `isrBotaoEmergencia()` | Interrupção (`IRAM_ATTR`) | Semáforo Binário + `portYIELD_FROM_ISR()` | Detecta borda de descida (`FALLING`) no botão com *debounce* de 250 ms e força troca de contexto imediata. | 
| **Prio 1** | `taskProdutora` | Baixa | `vTaskDelay` + `xQueueSend` | Lê temperatura e umidade do DHT11 a cada 2 s, envia o pacote (`struct`) para a fila e pisca o LED Verde. | 
| **Prio 2** | `taskConsumidora` | Média | `xQueueReceive` + `mutexSerial` | Aguarda bloqueada (sem gasto de CPU) novos itens na fila e exibe a telemetria formatada no Monitor Serial. | 
| **Prio 3** | `taskEmergencia` | Alta (Crítica) | `xSemaphoreTake` + `mutexSerial` | Preempta as tarefas de menor prioridade ao toque do botão, apaga o LED Verde e aciona o LED Vermelho por 1,5 s. | 

## Arquitetura Física e Pinagem

Montagem realizada em protoboard utilizando o microcontrolador **DOIT ESP32 DEVKIT V1**:

| Componente | Pino no ESP32 | Ligação Elétrica | Finalidade | 
| ----- | ----- | ----- | ----- | 
| **Sensor DHT11** | `GPIO 15` (`D15`) | `VCC` em `3V3`, `GND` no comum, `DATA` no `D15` | Aquisição digital de temperatura e umidade. | 
| **Botão Táctil** | `GPIO 13` (`D13`) | `INPUT_PULLUP` interno; fecha contato com `GND` | Gatilho de emergência via interrupção externa. | 
| **LED Verde** | `GPIO 12` (`D12`) | Ânodo (`+`) via resistor **220 Ohms** no `D12`; Cátodo (`-`) no `GND` | Indicador visual da tarefa periódica (`Prio 1`). | 
| **LED Vermelho** | `GPIO 14` (`D14`) | Ânodo (`+`) via resistor **220 Ohms** no `D14`; Cátodo (`-`) no `GND` | Indicador visual de alerta crítico (`Prio 3`). | 
| **Monitor Serial** | `UART0` (USB) | Taxa de transmissão em **115200 baud** | Saída de logs e telemetria protegida por Mutex. | 

## Como Executar o Projeto

### 1. Pré-requisitos de Software

1. Instale a **Arduino IDE** (versão 2.x ou superior).

2. Adicione o suporte às placas ESP32 no Gerenciador de Placas (`esp32 by Espressif Systems`).

3. Pelo Gerenciador de Bibliotecas (`Ctrl + Shift + I`), instale:

   * `DHT sensor library` (Adafruit)

   * `Adafruit Unified Sensor`

### 2. Compilação e Upload

1. Conecte o ESP32 via cabo USB ao computador.

2. Selecione a placa **DOIT ESP32 DEVKIT V1** e a respectiva porta `COM`.

3. Abra o arquivo principal do código (`.ino`), clique em **Upload** e abra o **Monitor Serial** configurado para **115200 baud**.

## Comportamento Esperado nos Testes

1. **Operação Normal:** A cada 2 segundos, o LED Verde pisca por 200 ms e o Monitor Serial imprime o número sequencial da leitura junto aos valores de temperatura ($^\circ\text{C}$) e umidade ($\%$).

2. **Preempção de Emergência:** Ao pressionar o botão no pino `D13` a qualquer instante, o escalonador interrompe imediatamente as tarefas de prioridade 1 e 2, apaga o LED Verde, acende o LED Vermelho por 1,5 segundo e exibe o alerta de preempção no Monitor Serial sem sobreposição de caracteres.

## 