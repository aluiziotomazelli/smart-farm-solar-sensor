# Solar Sensor — Design da Aplicação (Revisão de Arquitetura)

> Documento de design resultante da revisão de arquitetura do app `SolarSensor`.
> Complementa [`solar_sensor_architecture.md`](solar_sensor_architecture.md) (visão física/protocolo)
> e [`session-ses_011f.md`](session-ses_011f.md) (visão do `run()` atual).

---

## 1. Requisitos da aplicação

1. **Ler dados do sensor** — `InaSensorTask` (INA226, 4-8 Hz, EMA, delta).
2. **Enviar telemetria via ESP-NOW** quando necessário (delta ou heartbeat 1s). O pacote é primordial para o hub tomar decisões.
3. **Decidir entrada em deep sleep** (dusk/dawn) — com base em corrente + relógio.
4. **Receber comandos do hub** — `start_ota`, `reboot` forçado, `sync_time`.
5. **Processo de OTA completo** — parar o que for supérfluo (inclusive competição de rádio), conectar à internet, processar o OTA, desconectar, reiniciar; no boot seguinte verificar `pending verify`, aguardar o `init()` saudável, aceitar o firmware novo ou dar rollback.
6. **Verificar bateria** e enviar junto com os dados de leitura (não primordial — bateria é só backup). A `InaSensorTask` **não deve** ter acesso aos dados de bateria.

---

## 2. Estado atual (as-is) — fatos verificados no código

| Item | Situação |
|---|---|
| `SolarSensor::run()` | Stub (`solar_sensor.cpp:143-147`): loga e retorna. `main.cpp:162` chama uma vez. |
| `process_ina_samples()`, `enter_deep_sleep()`, `save_persistent_state()`, `recover_ina_hardware()`, `check_firmware()`, `send_ota_report()`, `connect_wifi_with_retry()` | Implementados e testados, **nunca chamados** (o `run()` não os orquestra). |
| Telemetria | A `InaSensorTask` envia, mas o `SolarSensorReport` sai incompleto: bateria/stats/`is_night_mode` = 0; `unix_time` = ms desde o boot (monotônico), **não** epoch. |
| `rx_queue_` (app_rx_queue) | Nunca é consumida — comandos do hub são **descartados**. |
| Triggers de OTA | `btn_trigger_.arm()` e `espnow_trigger_.arm()` **nunca são chamados**; `EspNowOtaTrigger::notify()` não é invocado por ninguém. OTA via trigger está 100% desligado. |
| `send_data(require_ack=false)` | Non-blocking: enfileira na TX queue interna do `EspNowManager` (task própria, prio 9). Bloqueia no máximo 100ms se a fila (30) estiver cheia; retorna `ESP_FAIL` e o produtor reenvia no ciclo seguinte. |
| `CoreStorage` | Já possui `has_valid_time`, `last_sync_unix_time_ms`, `power_profile`, `sleep_interval_s`, `last_wake` (`WakeSource`). |
| `TimeSyncCommand` (farm) | Layout idêntico ao `time_manager::TimeSyncPacket` (12 bytes packed) → cast direto no handler. |
| Protocolo de comandos | Dois níveis: **transporte** `espnow::CommandType` (`protocol_types.hpp`): `START_OTA` (0x01), `REBOOT` (0x02), `SET_REPORT_INTERVAL` (0x03). **Aplicação** `farm::CommandType` (`farm_protocol_types.hpp`): `SLEEP_OVERRIDE` (0x40), `PUMP_*` (0x41/0x42), `SYNC_TIME` (0x43). |
| Bateria | `main.cpp` cria `bat_monitor` mas **não injeta** no `SolarSensor`. |

---

## 3. Decisões de arquitetura (com justificativa)

### D1 — Quem envia a telemetria: a própria `InaSensorTask`

**Decisão:** a task continua sendo a produtora do envio.

**Justificativa:**
- O dado está mais fresco na task (sample acabou de ser lido) e não depende do agendamento do main.
- A preocupação "main ocupado com gravação flash atrasa o pacote" **já é resolvida pelo `EspNowManager`**: ele possui fila TX interna + task própria (prio 9). O produtor apenas enfileira — non-blocking.
- **O `EspNowManager` já é o dispatcher.** Uma classe/task intermediária de dispatch seria uma fila redundante na frente de outra fila (telemetria → queue dispatcher → queue EspNowManager → rádio).
- Só valeria um dispatcher se houvesse necessidade real de **prioridade** (ACK de OTA na frente da telemetria) ou **coalescência** (descartar heartbeat velho). Hoje: YAGNI. Revisitar se virar problema observado.

**Ressalva:** `send_data` pode bloquear até 100ms com a TX queue cheia — aceitável para telemetria; o código já trata falha de envio (não atualiza `last_reported_*` e reenvia no ciclo seguinte).

### D2 — Report completo: `TelemetrySnapshot` compartilhado (mutex)

**Decisão:** um struct de ~25 bytes protegido por mutex, escrito pelo main e lido pela task na hora do envio.

**Justificativa:** a task não pode (e não deve) ter acesso a bateria/stats — esses dados vivem no `SolarSensor`/`BatteryMonitor`. O snapshot transfere só os *valores*, sem acoplar a task a esses componentes. (Tempo é exceção: a task lê o `ITimeManager` diretamente para o `unix_time` — ver D4.)

### D3 — `power_profile`: não entra no snapshot

**Decisão:**
- A task **hardcoda `ALWAYS_ON`** — semanticamente correto, pois ela só envia quando `reporting_enabled_ && sampling_enabled_` (modo dia).
- O report final `DEEP_SLEEP` é **evento do main**: antes de `enter_deep_sleep()`, o main seta `core_.power_profile = DEEP_SLEEP`, monta o `SolarSensorReport` (profile `DEEP_SLEEP`, `is_night_mode=true`, bateria do snapshot) e envia com `require_ack=true` (pode bloquear — é a última coisa antes de dormir).

### D4 — `unix_time` do report: task calcula via `ITimeManager` no envio

**Decisão:** o `unix_time` do report é calculado pela task **no momento do envio**, lendo o relógio de sistema:

```cpp
// InaSensorTask::send_telemetry_report()
report.unix_time = time_.is_synchronized() ? time_.get_timestamp_ms() : 0;
```

A task ganha `time_manager::ITimeManager&` no construtor. O `TelemetrySnapshot` **não carrega nenhum campo de tempo** (nem `unix_time_ms`, nem ponto de sync).

**Justificativa:**
- `ITimeManager::get_timestamp_ms()` lê o relógio de sistema (`gettimeofday`), que no C3 é baseado no RTC e **continua contando durante o deep sleep** — a task obtém o epoch real no instante do envio, sem "ponto de sync" no snapshot nem aritmética monotônica. A premissa do D4 antigo (`esp_timer` sobreviver ao deep sleep) deixa de existir.
- O valor nunca fica defasado: é lido na hora, junto com o sample.
- O snapshot fica mais simples (só bateria/stats/`is_night_mode`).
- Fluxo coerente com o `CommandHandler`: `SYNC_TIME` → `time_manager.sync_from_time_packet()` acerta o relógio de sistema → o próximo report da task já reflete o epoch novo.

**Guard `is_synchronized()`:** sem sync o relógio de sistema está na base (epoch ~0), o que também dispararia o auto-sync do hub por diff enorme. Com o guard, o valor vai `0` = "não sincronizado" (mesma semântica do water-tank com `has_valid_time == false`).

**Responsabilidade do hub (fora deste repo — pode ser feito, o hub ainda não tem `solar_sensor_handler`):** **tolerar `unix_time == 0`** — não disparar auto-sync quando o valor for 0; a detecção de discrepância (diff > 5s) só deve valer para `unix_time > 0`. Sem isso, o `0` causaria tempestade de `sync_time` a cada report.

**Manter o campo no `SolarSensorReport`:** sim — remover seria mudança de protocolo (struct packed, afeta o hub) para economizar 8 bytes num pacote de 27. `0` já significa "não sincronizado".

### D5 — OTA não exige deinit do espnow

**Decisão:** durante o download, `espnow.set_channel_policy(ChannelPolicy::FIXED)` (AP domina o canal); ao falhar, volta `SCAN`. **A amostragem continua** (o watchdog do `process_ina_samples()` permanece vivo) — só o reporting é desligado.

**Justificativa:** a API `set_channel_policy` já existe exatamente para isso; evita o ciclo pesado de `deinit()`/`init()` do EspNowManager.

### D6 — Heartbeat interno do espnow: mantém 0

**Decisão:** a telemetria de 1s **é** o sinal de liveness. O node não precisa (nem deve) saber se está "offline" — isso é responsabilidade de quem monitora (hub). O node mantém `heartbeat_interval_ms = 0`.

**Esclarecimento (estado atual):** `get_offline_peers()` é método **público do `espnow_manager`** (não do hub app) e **ainda não é usado em lugar nenhum**. O hub não tem método próprio de detecção de offline. O `espnow_manager` já faz a detecção internamente, mas a API é pobre: retorna um `etl::vector` com **todos** os nodes offline — o hub precisaria iterar o vetor para saber se um node específico caiu.

**Pendência:** estudo futuro da API do `espnow_manager` (ver §9). Não muda nada no solar sensor por ora.

### D7 — `SLEEP_OVERRIDE`: não se aplica

**Decisão:** comando ignorado (ACK `ERROR_INVALID_DATA`) — é para nodes como o water-tank. O solar sensor não usa `sleep_interval_s` override.

---

## 4. Estrutura proposta (target)

```
                    ┌─────────────────────────────────────────────┐
                    │           SolarSensor (coordenador)         │
                    │  run(): loop ~7Hz (block no sample queue)   │
                    │   ├─ TelemetrySnapshot (escreve)            │
                    │   ├─ CommandHandler.process()               │
                    │   ├─ OtaCoordinator.tick()                  │
                    │   ├─ DayNightController.update()            │
                    │   ├─ battery read (periódico)               │
                    │   └─ save_persistent_state (15min)          │
                    └──────────────┬──────────────────────────────┘
                       samples q   │                │ battery/time/sync
                                  ▼                ▼
                    ┌────────────────────────┐  ┌──────────────────────┐
                    │ InaSensorTask (task)   │  │ BatteryMonitor       │
                    │  - sample + telemetria │  │ TimeManager (sync)   │
                    │  - lê snapshot (mutex) │  └──────────────────────┘
                    └───────────┬────────────┘
                                │ send_data (non-blocking)
                                ▼
                    ┌─────────────────────────────┐
                    │ EspNowManager (JÁ é o       │
                    │ dispatcher: fila + tasks)   │
                    └─────────────────────────────┘
```

**Princípios:**
- **Nenhuma task nova.** O TX já tem task própria no EspNowManager.
- **Nenhuma classe de dispatch nova.** Estado compartilhado para o que é estado; send direto para o que é evento.
- `SolarSensor` vira coordenador fino: o `run()` é um script que faz tick em subsistemas coesos.

---

## 5. Detalhamento por classe

### 5.1 `TelemetrySnapshot` (nova — peça-chave)

```cpp
struct TelemetrySnapshotData {   // ~25 bytes, protegido por mutex
    uint16_t    battery_mv;
    uint8_t     battery_percent;
    farm::BatteryState battery_state;
    uint16_t    max_current_ma;
    uint32_t    daily_yield_mah;
    bool        is_night_mode;
};
```

- **Quem escreve:** main (bateria a cada N segundos, stats após processar samples, `is_night_mode` nas transições).
- **Quem lê:** `InaSensorTask::send_telemetry_report()` (lock → copy → unlock → preenche report; `unix_time` vem do `ITimeManager` — ver D4).
- **Sincronização:** C3 é single-core → mutex simples ou critical section resolve.
- **Injeção:** referência no construtor da task (e no `SolarSensor`). O construtor da task também ganha `time_manager::ITimeManager&` (D4) → atualizar host tests (inclui criar `MockTimeManager`).
- **`power_profile` fica FORA** (ver D3).

### 5.2 `CommandHandler` (nova, pequena, poling do `run()`)

```cpp
CommandHandler(QueueHandle_t rx_queue, IEspNowManager& espnow, ITimeManager& time,
               IOtaTrigger& espnow_ota_trigger, ISystemHAL& hal_system, CoreStorage& core);

void process();   // drena rx_queue_ (chamado no loop ~7Hz)
```

O comando chega no `AppMessage` com `msg_type = COMMAND` e o valor do `CommandType` **no campo `msg.payload_type`** (o transporte encoda o comando nesse campo do header).

| `msg.payload_type` | Ação |
|---|---|
| `espnow::CommandType::START_OTA` (0x01) | `espnow_ota_trigger.notify()` → `OtaCoordinator`. |
| `espnow::CommandType::REBOOT` (0x02) | `hal_system.restart()`. |
| `espnow::CommandType::SET_REPORT_INTERVAL` (0x03) | Fora dos requisitos atuais — ACK `ERROR_INVALID_DATA` (candidato futuro: ajustar o `heartbeat_interval_ms` da task). |
| `farm::CommandType::SYNC_TIME` (0x43) | `time_.sync_from_time_packet(*reinterpret_cast<const TimeSyncPacket*>(msg.payload))`; atualiza `core_.has_valid_time` / `core_.last_sync_unix_time_ms`. |
| `farm::CommandType::SLEEP_OVERRIDE` / `PUMP_*` (0x40-0x42) | Ignora (ACK `ERROR_INVALID_DATA`). |
| qualquer | Se `msg.requires_ack` → `espnow.confirm_reception(msg.sender_id, msg.sequence_number, ok/err)`. |

### 5.3 `OtaCoordinator` (extração — a maior, implementar por último)

Máquina de estados (transpõe reboot):

```
IDLE →(trigger: botão/espnow)→ PREPARING:
         - ina_task.set_reporting_enabled(false)
         - espnow.set_channel_policy(FIXED)
         - wifi connect (retry)
      → DOWNLOADING:
         - ota_manager.start_ota()
         - poll get_status() até DONE/FAILED   [samples continuam → watchdog vivo]
      → SUCCESS → hal_system.restart() → VERIFYING (próximo boot)
      → FAILED  → espnow.set_channel_policy(SCAN); reporting on → IDLE

VERIFYING (boot):
  pending_firmware_verify_ && session_healthy → confirm_app_valid()
      + report OTA_STATUS_REPORT(CONFIRMED_SUCCESS) + commit versão → IDLE
  senão → report OTA_STATUS_REPORT(ROLLBACK_TRIGGERED, HEALTH_CHECK_FAILED)
      + rollback_and_reboot()
```

**Deps:** `ota_manager`, `wifi`, `espnow` (channel policy + `send_data` para o report), `ina_task` (só `set_reporting_enabled`), `hal_system`, `core_`.

**Alternativa de desacoplamento:** interface `IOtaApp` (implementada pelo `SolarSensor`) para os side-effects (`set_telemetry_enabled`, channel policy, reboot). Como os componentes já têm interfaces com mocks no repo, injetar direto é menos abstração nova — decidir na implementação.

### 5.4 `DayNightController` (nova, lógica pura — melhor custo/benefício)

Sem I/O, só decisões → testável sem mock.

- `update(current_ma)`: se dia e `current < threshold` → request sleep.
  - **Com clock válido:** confirma também que a hora está dentro da janela de confiança (**dusk = corrente + relógio**).
  - **Sem clock válido:** usa só corrente — ver D8 abaixo.
- `classify_wake(cause, now)`:
  - `GPIO` (ALERT low) → dia (amanhecer).
  - `TIMER` na hora de calibração → **modo calibração**: ler shunt zero offset (madrugada, sem luz), persistir, voltar a dormir.
  - `TIMER` fora da janela → voltar a dormir.
- **Config:** `dusk_current_threshold_ma` (ver D8), `dusk_hour_window`, `calibration_wake_hour` — `dusk_hour_window` e `calibration_wake_hour` a medir com sensor real (pendente).

### 5.5 `StatsManager` — **adiado**

Stats + NVS permanecem no `SolarSensor`. Extrair apenas quando a integral diária (`daily_yield_mah`) e a calibração de offset ganharem lógica real. O snapshot já desacopla stats da task.

### 5.6 `SolarSensor` (após refactor)

- Construtor perde ~6-7 deps (absorvidas pelos subsistemas).
- `run()` vira orquestração de ticks.
- Mantém: watchdog/recuperação INA, transições de sleep, storage (por ora), composição.

---

## 6. Gaps / pendências de protocolo e código

1. **`START_OTA`/`REBOOT`/`SET_REPORT_INTERVAL` já existem** em `espnow::CommandType` (`protocol_types.hpp`) — não há gap de protocolo para esses. O gap real é de **wiring**: `rx_queue_` não é consumida (comandos nunca despachados) e os triggers não são armados.
2. **Triggers nunca são armados** (`btn_trigger_.arm()`, `espnow_trigger_.arm()`) e `EspNowOtaTrigger::notify()` não é chamado por ninguém → wire do gatilho de OTA é 100% novo.
3. **`unix_time` do report é monotônico**, não epoch — placeholder até a task ler o `ITimeManager` no envio (D4) e o hub tolerar `unix_time == 0`.
4. **`rx_queue_` nunca consumida** → comandos descartados (requisito 4 inteiro em falta).
5. **Bateria não injetada** no `SolarSensor` → campos de bateria sempre 0.
6. **API de offline do `espnow_manager`** — `get_offline_peers()` é público mas não usado; hub não tem detecção própria. Ver §9 (estudo futuro; nada a mudar no node por ora).
7. **Dusk sem clock válido** — comportamento a definir (ver §7).

---

## 7. Decisões em aberto / fechadas

1. **Indireção do `START_OTA`:** manter o `EspNowOtaTrigger` (CommandHandler chama `notify()` → listener) ou o `CommandHandler` chamar o `OtaCoordinator` direto? (Mantive o trigger por reutilizar a abstração `IOtaTrigger` já existente.) — **EM ABERTO**
2. **Dusk sem clock válido** → **FECHADO (D8 abaixo)**.
3. **Granularidade da implementação:** incremental — **FECHADO (§8 abaixo)**.

### D8 — Dusk sem clock válido: corrente abaixo de `DEFAULT_DUSK_CURRENT_MA`

**Decisão:** quando `ITimeManager::is_synchronized() == false`, o `DayNightController`
decide entrar em sleep baseando-se **apenas na corrente**. Threshold inicial:

```cpp
// ina_sensor_types.hpp
/// Corrente abaixo da qual o node considera que é entardecer (dusk).
/// Valor inicial conservador; calibrar com sensor real no campo.
static constexpr uint16_t DEFAULT_DUSK_CURRENT_MA = 1; // < 1 mA → entra em sleep
```

**Justificativa:**
- O node deve ser capaz de entrar em deep sleep mesmo sem nunca ter recebido
  `SYNC_TIME` do hub (ex.: primeiro deploy, hub offline).
- 1 mA é abaixo do ruído de dark current do painel — qualquer irradiância
  real gera corrente muito maior. Conservador o suficiente para não dormir
  durante o dia por falso negativo.
- A constante vive em `ina_sensor_types.hpp`, junto a `DEFAULT_DAWN_WAKEUP_ALERT_LIMIT`
  (mesma convenção: limites de corrente relacionados ao ciclo dia/noite do INA).
- Calibrar o valor real (e a janela de hora) com multímetro/sensor no campo;
  atualizar a constante e o comentário de cálculo no mesmo estilo de
  `DEFAULT_DAWN_WAKEUP_ALERT_LIMIT`.

**Com clock válido:** a janela de hora é AND com a corrente — evita dormir
num pico de nuvem passageira no meio do dia.

---

## 8. Sequência de implementação (incremental, TDD)

> Regra do repo: nenhuma lógica sem teste correspondente (host_test, GoogleTest/GoogleMock).

1. **`TelemetrySnapshot` + `ITimeManager` na task (1a)** — testes: main escreve / task lê; report completo; `unix_time` real via `ITimeManager` (sincronizado) / `0` (não sincronizado). Muda construtor da task — `TelemetrySnapshot&` + `ITimeManager&` → ajustar `SolarSensorTaskTest` e criar `MockTimeManager`.

2. **`BatteryMonitor` injetado no `SolarSensor` (1b)** — adiciona `IBatteryMonitor&` ao construtor de `SolarSensor` e ao wiring em `main.cpp`. Leitura periódica de bateria no `run()` → escreve no snapshot. Testes: snapshot com bateria real; campos não-zero no report.
   > ⚠️ **Atenção:** Os passos 1a e 1b podem ser commitados em qualquer ordem, mas o report só fica completo com os dois aplicados. Enquanto 1b não estiver feito, os campos de bateria do snapshot são inicializados dos valores persistidos no `SolarStats` (carregados no boot via NVS/RTC), o que é aceitável como fallback.

3. **`CommandHandler`** — testes: dispatch `SYNC_TIME`, ignore `SLEEP_OVERRIDE`, ack `confirm_reception`. (Desbloqueia requisito 4.)

4. **`DayNightController`** — testes: dusk só por corrente (`DEFAULT_DUSK_CURRENT_MA`), dusk por corrente+hora (clock válido), `classify_wake` GPIO/TIMER/calibração. (Lógica pura, sem mock.)

5. **`run()` integrando 1-4** — testes de app: drena fila, watchdog restart, dusk → sleep, wake GPIO → dia.
   > **Atenção:** `ota_triggered_` (flag atômica em `SolarSensor`) é setada em `on_ota_triggered()` mas nunca lida. Antes do `OtaCoordinator` existir, o `run()` deve checar essa flag e chamar `espnow_trigger_.notify()` / iniciar o fluxo de OTA inline.

6. **`OtaCoordinator`** — por último (maior; a versão atual dentro do `SolarSensor` já funciona): trigger armado, channel policy, download, verify no boot, rollback.

---

## 9. Estudos futuros (fora do escopo do SolarSensor)

### 9.1 API de detecção de offline no `espnow_manager`

- **Problema:** o `espnow_manager` já detecta offline internamente (e bem), mas a API só expõe `get_offline_peers()` — um `etl::vector` com **todos** os peers offline. O hub precisa iterar o vetor para saber se um node específico está offline.
- **Proposta:** métodos dedicados na API, ex.: `bool is_node_offline(NodeId)` / `is_node_online(NodeId)`, sem expor o vetor.
- **Pergunta em aberto:** a detecção de offline considera **qualquer pacote recebido** (inclusive os internos de comunicação automática — scan, pairing, ack) ou depende do `heartbeat_interval_ms` configurado por peer? Evidência no código: `NOTIFY_LINK_ALIVE` é setado em qualquer recepção válida de pacote (`protocol_types.hpp`). Isso determina se a telemetria de 1s do solar sensor mantém o peer "vivo" no hub.
- **Impacto no solar sensor:** nenhum por ora — o node continua com `heartbeat_interval_ms = 0` (telemetria é o liveness). O estudo é do lado espnow_manager/hub.
