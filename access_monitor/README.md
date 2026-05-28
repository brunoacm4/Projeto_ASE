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
source /home/bruno/esp/esp-idf/export.sh
idf.py -B build_esp32c6 build
```

## Flash

```sh
source /home/bruno/esp/esp-idf/export.sh
idf.py -B build_esp32c6 -p /dev/ttyACM0 flash monitor
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

O ficheiro CSV usado no cartao SD e:

```text
/sdcard/LOG.CSV
```

Foi escolhido um nome curto porque a configuracao atual do FatFs nao usa nomes longos.

## Cartoes autorizados

O UID `DEAF32F6` esta autorizado em `main/access_control.c`.

## MQTT

O MQTT fica desligado por defeito para validar primeiro DHT20, SD, TFT, RFID e deep-sleep.

Para ligar:

```sh
source /home/bruno/esp/esp-idf/export.sh
idf.py -B build_esp32c6 menuconfig
```

Depois ativa `Access Monitor Configuration -> Enable MQTT publishing` e configura o broker em `MQTT broker URL`.

Por defeito o projeto usa MQTT com TLS:

```text
mqtts://broker.hivemq.com:8883
```

Para HiveMQ publico, o firmware usa o certificate bundle do ESP-IDF para validar o certificado do broker. No MQTTX usa o mesmo host, porta `8883`, `SSL/TLS` ligado e seleciona `CA signed server certificate`.

O certificado `main/mosquitto_org.crt` fica guardado no projeto apenas como referencia do Lab 9/Mosquitto.

## Nota de pinout

Os pinos estao centralizados em `main/project_config.h`.

## Deep-sleep e espera RFID

O sistema acorda de deep-sleep por timer ou PIR. Durante a janela de leitura RFID, a implementacao atual faz espera ativa com `vTaskDelay()` entre polls do MFRC522. Nao ha light-sleep real nesta versao porque o IRQ do RFID ficou desligado; para acordar de light-sleep quando um cartao chega seria necessario ligar esse IRQ a um GPIO de wake.
