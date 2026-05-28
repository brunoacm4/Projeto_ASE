# Access Monitor

Projeto ESP-IDF para o Sistema de Controlo de Acesso e Monitorizacao Ambiental.

## Base reutilizada das aulas

- TFT ST7735: `AULA_10/Driver-ST7735-ESP32-C6/components/st7735_driver`
- DHT20: `AULA_4/main/multisensor_dht20.c/.h`
- SD via SPI: adaptado de `AULA_5V2/main/sd_card_example_main.c`
- Deep-sleep/EXT1: adaptado de `AULA7/ds_wktimer` e `AULA7/ds_wkext1`
- MQTT: adaptado de `AULA_9/mqtt_tcp_lab`

## Build

```sh
source /home/tito/esp/esp-idf/export.sh
idf.py build
```

Se o build antigo tiver sido configurado com outro ambiente Python do ESP-IDF, executa uma vez:

```sh
idf.py fullclean
idf.py build
```

## Flash

```sh
source /home/tito/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash monitor
```

Altera a porta se a board aparecer noutro dispositivo. A consola principal esta configurada para USB Serial/JTAG para libertar GPIO16 e GPIO17.

## Pinout confirmado

| Modulo | Sinal | GPIO |
|---|---|---:|
| DHT20 | SDA | 17 |
| DHT20 | SCL | 23 |
| PIR | OUT | 4 |
| SPI comum | MOSI | 19 |
| SPI comum | MISO | 20 |
| SPI comum | SCK | 21 |
| SD | CS | 18 |
| TFT | CS | 22 |
| TFT | DC | 2 |
| TFT | RST | 3 |
| TFT | LIT/BL | 15 |
| RFID MFRC522 | SDA/CS | 16 |
| RFID MFRC522 | RST | 3 |

O IRQ do RFID fica livre nesta versao. SD, TFT e RFID partilham o mesmo barramento SPI e usam CS separados.

## Logs

Os ficheiros CSV usados no cartao SD sao:

```text
/sdcard/LOG.CSV
/sdcard/ACCESS.CSV
/sdcard/PRESENT.CSV
```

Foram escolhidos nomes curtos porque a configuracao atual do FatFs nao usa nomes longos.

- `LOG.CSV`: eventos gerais, ambiente e erros.
- `ACCESS.CSV`: entrada, saida, negados, timeouts e evacuacoes.
- `PRESENT.CSV`: snapshot dos cartoes atualmente dentro.

## Cartoes autorizados

O UID `DEAF32F6` esta autorizado em `main/access_control.c`.

## Presenca e porta virtual

O sistema usa um unico leitor RFID. Cada UID autorizado alterna o seu estado:

- se nao estava dentro, o evento e `entry`;
- se ja estava dentro, o evento e `exit`.

O LED RGB onboard da ESP32-C6, ligado ao GPIO8, indica a porta virtual:

- verde: entrada ou saida validada;
- vermelho: acesso negado por cartao;
- apagado: timeout ou repouso.

Quando o PIR acorda o sistema e nenhum cartao e apresentado, o TFT mostra apenas `TIMEOUT`.

Em alerta ambiental, o TFT mostra o aviso, todos os cartoes presentes sao evacuados e `PRESENT.CSV` e limpo. O LED nao acende a verde neste caso.

## Periodicidade do TFT

O sistema acorda por timer a cada 20 s para leitura ambiental. Quando o TFT acende para mostrar temperatura/humidade, a informacao fica visivel durante 5 s. Se existirem cartoes dentro, o ecrã de presenca tambem fica visivel durante 5 s.

## MQTT e dashboard

O `sdkconfig` atual tem MQTT ativo, mas as credenciais Wi-Fi ficam como `CONFIGURE_ME`. Configura a rede e o broker em `menuconfig` antes de testar MQTT.

Para configurar:

```sh
source /home/tito/esp/esp-idf/export.sh
idf.py menuconfig
```

Opcoes relevantes:

- `Access Monitor Configuration -> Enable MQTT publishing`
- `Access Monitor Configuration -> MQTT broker URL`
- `Example Connection Configuration -> WiFi SSID`
- `Example Connection Configuration -> WiFi Password`

Para a dashboard local, arranca a stack no PC. Por defeito, a bridge liga-se ao broker MQTT que ja esta no host em `localhost:1883`:

```sh
cd dashboard
docker compose down
docker compose up --build
```

O `down` e importante depois de alteracoes na bridge, porque remove containers antigos que possam ter ficado em erro antes de o Mosquitto estar pronto.

Se nao tiveres nenhum broker MQTT no host e quiseres arrancar tambem o Mosquitto Docker, usa:

```sh
cd dashboard
MQTT_HOST=mosquitto docker compose --profile broker up --build
```

Nesse modo, a porta `1883` do host tem de estar livre. Se ja houver um Mosquitto local nessa porta, usa o modo default acima.

Depois configura o broker do firmware para:

```text
mqtt://<IP_DO_PC>:1883
```

A dashboard fica em:

```text
Grafana:    http://localhost:3000  admin/admin
Prometheus: http://localhost:9090
Metrics:    http://localhost:9100/metrics
MQTT:       localhost:1883
```

Topicos publicados:

- `ase/access/events`
- `ase/access/env`
- `ase/access/alerts`
- `ase/access/status`
- `ase/access/presence`

O certificado `main/mosquitto_org.crt` fica guardado no projeto apenas como referencia do Lab 9/Mosquitto.

### Diagnostico MQTT / Prometheus / Grafana

1. Confirma o IP do PC onde corre a dashboard:

```sh
hostname -I
```

2. No firmware, confirma em `menuconfig` que o broker esta assim:

```text
CONFIG_PROJECT_BROKER_URL=mqtt://<IP_DO_PC>:1883
```

3. Arranca a dashboard. Se ja tens MQTTX a ver mensagens em `localhost:1883`, usa o modo default:

```sh
cd dashboard
docker compose down
docker compose up --build
```

O servico `bridge` deve imprimir mensagens como `connecting to MQTT broker host.docker.internal:1883`, `connected`, `subscribed` e `message received`.

Se preferires usar o Mosquitto dentro do Docker, garante primeiro que a porta `1883` esta livre e arranca com:

```sh
cd dashboard
MQTT_HOST=mosquitto docker compose --profile broker up --build
```

4. Verifica se chegam mensagens MQTT:

```sh
mosquitto_sub -h localhost -t 'ase/access/#' -v
```

Tambem podes confirmar isto no MQTTX subscrevendo `ase/access/#`.

5. Testa a dashboard sem a ESP, publicando uma mensagem manual:

```sh
mosquitto_pub -h localhost -t ase/access/events -m '{"event":"entry","card_id":"TESTE01","direction":"entry","result":"granted","occupancy":1,"temp":24.5,"hum":55.0,"blocked":0,"millis":1234}'
```

Se estiveres a usar o Mosquitto Docker com `--profile broker`, podes usar os mesmos comandos via `docker compose exec mosquitto ...`.

6. Confirma que o bridge recebeu dados:

```text
http://localhost:9100/metrics
```

A metrica `access_messages_total` deve aumentar.

7. Confirma no Prometheus:

```text
http://localhost:9090/targets
```

O target `access-monitor` deve estar `UP`.

## Nota de pinout

Os pinos estao centralizados em `main/project_config.h`.

## Deep-sleep e espera RFID

O sistema acorda de deep-sleep por timer ou PIR. Durante a janela de leitura RFID, a implementacao atual faz espera ativa com `vTaskDelay()` entre polls do MFRC522. Nao ha light-sleep real nesta versao porque o IRQ do RFID ficou desligado; para acordar de light-sleep quando um cartao chega seria necessario ligar esse IRQ a um GPIO de wake.
