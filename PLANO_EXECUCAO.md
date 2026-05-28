# Plano de Execucao - Sistema de Controlo de Acesso e Monitorizacao Ambiental

## 1. Objetivo do projeto

Construir um sistema assíncrono para ESP32-C6 que permanece em deep-sleep na maior parte do tempo e acorda por:

- PIR: evento externo de movimento na entrada.
- Timer RTC: leitura ambiental periodica.

Quando acorda por timer, o sistema le temperatura/humidade no DHT20, regista o evento no SD, publica no MQTT se houver conectividade e regressa a deep-sleep. Se os limites forem excedidos, entra em estado de alerta e bloqueia preventivamente o acesso.

Quando acorda por PIR, o sistema liga o TFT, apresenta estado de espera, aguarda RFID em espera ativa com pequenos `vTaskDelay()` entre polls, valida o cartao, apresenta acesso autorizado/negado e regista o log em SD e MQTT.

A porta e virtual: a abertura e assinalada no TFT e no log.

## 2. Ficheiros das aulas a usar como base

| Funcionalidade | Base das aulas | Ficheiros a reutilizar |
|---|---|---|
| DHT20 via I2C | Aula 4 e Aula 5V2 | `AULA_4/main/multisensor_dht20.c`, `AULA_4/main/multisensor_dht20.h` ou copias equivalentes em `AULA_5V2/main/` |
| SD card via SPI | Aula 5 / Lab 6 | `AULA_5V2/main/sd_card_example_main.c` como base principal, porque ja integra SD + DHT20 |
| TFT ST7735 | Aula 10 | `AULA_10/Driver-ST7735-ESP32-C6/components/st7735_driver/` e `AULA_10/Driver-ST7735-ESP32-C6/main/main.c` |
| Deep-sleep timer | Aula 7 | `AULA7/ds_wktimer/main/main.c` |
| Deep-sleep EXT1 | Aula 7 | `AULA7/ds_wkext1/main/main.c` |
| Light-sleep GPIO/timer | Aula 7 | `AULA7/ls_wkgpio/main/main.c`, `AULA7/ls_wktimer/main/main.c` como referencia teorica; nao usado na versao atual porque o IRQ do RFID fica livre |
| MQTT TCP | Aula 9 | `AULA_9/mqtt_tcp_lab/main/app_main.c` |
| Wi-Fi STA | Aula 8 / MQTT helper | configuracao `example_connect()` usada no exemplo MQTT |
| GPIO / input simples | Aula 3 | padrao de configuracao de GPIO input com pull-up |

O projeto deve nascer preferencialmente de `AULA_10/Driver-ST7735-ESP32-C6`, porque ja contem o componente `st7735_driver` pronto. Depois copia-se para esse projeto o codigo de SD/DHT20/MQTT/low-power.

## 3. Arquitetura proposta

### Modulos de software

- `main.c`: maquina de estados, wake-up cause e arranque do fluxo principal.
- `multisensor_dht20.c/.h`: driver DHT20 reaproveitado das aulas.
- `storage_log.c/.h`: montagem SD, criacao de cabecalho CSV e escrita de logs.
- `display_ui.c/.h`: ecras TFT: standby, ambiente, alerta, RFID, acesso autorizado/negado.
- `mqtt_app.c/.h`: Wi-Fi + MQTT, publicacao de eventos.
- `pir.c/.h`: configuracao GPIO/RTC wake para PIR.
- `rfid.c/.h`: driver simples para o leitor RFID, provavelmente MFRC522 via SPI.
- `access_control.c/.h`: lista de cartoes autorizados e validacao.
- `project_config.h`: pinos, limites, topicos MQTT e parametros de sleep.

### Maquina de estados

Estados principais:

- `STATE_BOOT`: inicializacao minima e identificacao da causa de wake-up.
- `STATE_ENV_SAMPLE`: acordou por timer, le DHT20.
- `STATE_ENV_ALERT`: limites ambientais excedidos, acesso bloqueado.
- `STATE_PIR_DETECTED`: acordou por movimento.
- `STATE_WAIT_RFID`: TFT ligado e sistema a aguardar cartao.
- `STATE_ACCESS_GRANTED`: cartao autorizado, abertura virtual.
- `STATE_ACCESS_DENIED`: cartao desconhecido.
- `STATE_LOG_SYNC`: escrita SD e publicacao MQTT quando aplicavel.
- `STATE_DEEP_SLEEP`: configuracao das fontes de wake-up e entrada em deep-sleep.

Fluxo por timer:

1. `STATE_BOOT`
2. detectar `ESP_SLEEP_WAKEUP_TIMER`
3. inicializar I2C/DHT20 e SD
4. ler temperatura/humidade
5. se `temp > TEMP_MAX` ou `hum > HUM_MAX`, marcar `access_blocked = true`
6. gravar log ambiental em SD
7. opcionalmente publicar MQTT
8. regressar a deep-sleep

Fluxo por PIR:

1. `STATE_BOOT`
2. detectar `ESP_SLEEP_WAKEUP_EXT1`
3. inicializar TFT, SD, RFID e opcionalmente MQTT
4. mostrar ecrã "MOVIMENTO / APROXIME CARTAO"
5. aguardar RFID durante uma janela definida, por exemplo 15 s
6. se cartao autorizado e ambiente sem alerta: mostrar "ACESSO AUTORIZADO"
7. se cartao desconhecido ou ambiente bloqueado: mostrar "ACESSO NEGADO"
8. gravar log de acesso em SD
9. publicar evento MQTT
10. regressar a deep-sleep

## 4. Pinos confirmados

Pinout fixado para a montagem atual:

| Sinal | GPIO | Origem |
|---|---:|---|
| I2C SDA DHT20 | 17 | Confirmado no hardware |
| I2C SCL DHT20 | 23 | Confirmado no hardware |
| SPI MOSI comum | 19 | Print / Aula 5V2 / Aula 10 |
| SPI MISO comum | 20 | Print / Aula 5V2 |
| SPI SCLK comum | 21 | Print / Aula 5V2 / Aula 10 |
| SD CS | 18 | Print / Aula 5V2 |
| TFT CS | 22 | Print / Aula 10 |
| TFT DC | 2 | Print / Aula 10 |
| TFT RST | 3 | Print / Aula 10 |
| TFT BL/LIT | 15 | Print / Aula 10 |
| RFID CS/SDA | 16 | Confirmado no hardware |
| RFID IRQ | livre | Nao usado nesta versao |
| RFID RST | 3 | Confirmado no hardware, partilhado com TFT RST |
| PIR OUT | 4 | Confirmado no hardware |

Notas:

- TFT, SD e RFID devem partilhar MOSI/MISO/SCLK e ter `CS` separados.
- A consola ESP-IDF deve usar USB Serial/JTAG para libertar GPIO16 e GPIO17 para RFID CS e DHT20 SDA.
- O RFID MFRC522 fica em espera ativa durante a janela de leitura. Light-sleep real so deve ser adicionado se o IRQ do RFID for ligado a um GPIO de wake.
- Se o RFID for PN532 via I2C, o desenho muda: pode partilhar o barramento I2C com DHT20, desde que os enderecos nao colidam.

## 5. Formato de logs

Usar `/sdcard/LOG.CSV`, porque a configuracao atual do FatFs nao usa nomes longos.

Cabecalho sugerido:

```csv
millis,wakeup,event,temp_c,humidity_pct,card_uid,result,access_blocked
```

Exemplos:

```csv
1230,timer,env_sample,24.10,58.40,,ok,0
8450,timer,env_alert,35.70,62.00,,blocked,1
15420,pir,rfid_auth,24.30,57.90,04A1B2C3,granted,0
20110,pir,rfid_auth,24.20,58.10,55EE12AA,denied,0
```

## 6. Topicos MQTT

Broker configurado por `menuconfig`, seguindo a Aula 9.

Topicos sugeridos:

- `ase/access/events`: acessos autorizados/negados.
- `ase/access/env`: leituras ambientais.
- `ase/access/alerts`: alertas ambientais e bloqueios.
- `ase/access/status`: estado do dispositivo.

Payload simples em JSON:

```json
{"event":"rfid_auth","uid":"04A1B2C3","result":"granted","temp":24.3,"hum":57.9}
```

## 7. Ordem de implementacao

### Fase 1 - Projeto base

1. Copiar `AULA_10/Driver-ST7735-ESP32-C6` para uma nova pasta do projeto.
2. Manter o componente `components/st7735_driver`.
3. Confirmar build e teste do TFT sem alterar a logica.

Resultado esperado: TFT inicializa e mostra texto/cores como na Aula 10.

### Fase 2 - DHT20

1. Copiar `multisensor_dht20.c/.h` da Aula 4 ou Aula 5V2.
2. Integrar leitura no projeto base.
3. Mostrar temperatura/humidade no TFT.

Resultado esperado: valores reais do DHT20 no TFT, como no exercicio final da Aula 10.

### Fase 3 - SD card

1. Adaptar `init_sd_card()` e `ensure_log_header()` de `AULA_5V2/main/sd_card_example_main.c`.
2. Criar `storage_log.c/.h`.
3. Escrever eventos em CSV.

Resultado esperado: `/sdcard/LOG.CSV` com cabecalho e linhas novas.

### Fase 4 - Low power

1. Integrar `esp_sleep_enable_timer_wakeup()` da Aula 7.
2. Integrar `esp_sleep_enable_ext1_wakeup_io()` da Aula 7 para PIR.
3. Usar `esp_sleep_get_wakeup_cause()` no arranque.
4. Preservar contadores em `RTC_DATA_ATTR` quando necessario.

Resultado esperado: acordar alternadamente por timer e por PIR/botao de teste.

### Fase 5 - PIR

1. Implementar `pir_init_for_wakeup()`.
2. Testar primeiro com botao, depois com sensor PIR.
3. Confirmar se o PIR fica ativo em nivel alto ou baixo e ajustar `ESP_EXT1_WAKEUP_ANY_HIGH` ou `ESP_EXT1_WAKEUP_ANY_LOW`.

Resultado esperado: movimento acorda o ESP32-C6 de deep-sleep.

### Fase 6 - RFID

1. Confirmar modelo do leitor.
2. Se for MFRC522, implementar driver SPI minimo:
   - reset
   - leitura/escrita de registos
   - deteccao de cartao
   - leitura do UID
3. Criar lista local de UIDs autorizados.
4. Integrar timeout de espera.

Resultado esperado: UID aparece no log e no TFT; cartoes autorizados sao distinguidos. O cartao `DEAF32F6` deve constar na lista autorizada.

### Fase 7 - MQTT

1. Adaptar `AULA_9/mqtt_tcp_lab/main/app_main.c`.
2. Inicializar NVS, netif, event loop e `example_connect()`.
3. Criar funcao `mqtt_publish_event(topic, payload)`.
4. Comecar com MQTT/TCP; TLS fica como melhoria se houver tempo.

Resultado esperado: eventos aparecem no broker/MQTTX.

### Fase 8 - Integracao final

1. Implementar a maquina de estados completa.
2. Garantir que cada ciclo termina em `STATE_DEEP_SLEEP`.
3. Validar prioridades:
   - alerta ambiental bloqueia acesso
   - PIR acorda imediatamente
   - logs sao persistidos mesmo sem MQTT
   - MQTT falha sem impedir SD/local

Resultado esperado: sistema autonomo, demonstravel e coerente com a proposta.

## 8. Plano de testes

Testes unitarios/manuais por modulo:

1. DHT20: ler valores plausiveis.
2. TFT: mostrar estados `NORMAL`, `ALERT`, `WAIT RFID`, `GRANTED`, `DENIED`.
3. SD: criar ficheiro, escrever cabecalho, acrescentar linhas.
4. Deep-sleep timer: acordar a cada intervalo configurado.
5. Deep-sleep PIR: acordar por botao e depois por PIR.
6. RFID: ler UID e comparar com lista autorizada.
7. MQTT: publicar eventos em topicos definidos.
8. Integracao: executar cenarios completos.

Cenarios finais:

- Sem movimento, temperatura normal: acorda por timer, regista ambiente, dorme.
- Temperatura alta: acorda por timer, cria alerta, bloqueia acesso, dorme.
- Movimento + cartao autorizado + ambiente normal: acesso autorizado.
- Movimento + cartao desconhecido: acesso negado.
- Movimento + cartao autorizado + ambiente bloqueado: acesso negado por alerta.
- Sem Wi-Fi/broker: sistema continua a funcionar e grava SD.

## 9. Riscos tecnicos

- O RFID MFRC522 partilha SPI com TFT/SD; e obrigatorio gerir corretamente os pinos `CS`.
- Deep-sleep reinicia o programa; qualquer estado persistente precisa de `RTC_DATA_ATTR`, SD ou NVS.
- Wi-Fi/MQTT consome bastante energia; deve ser usado apenas quando necessario.
- Se o PIR mantiver o nivel ativo durante muito tempo, o sistema pode acordar repetidamente; pode ser necessario debounce/tempo morto.
- O SD deve ser fechado com `fclose()` apos cada log para reduzir perda de dados antes de deep-sleep.
- MQTT nao deve ser dependencia critica: SD e TFT devem funcionar mesmo sem rede.

## 10. Entregaveis sugeridos

- Codigo ESP-IDF organizado por modulos.
- Esquema de ligacoes/pinout.
- Video/demo dos cenarios principais.
- Ficheiro CSV de exemplo retirado do SD.
- Screenshot/MQTTX com mensagens publicadas.
- Relatorio explicando a reutilizacao dos ficheiros das aulas.
