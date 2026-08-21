# HiQSDR FLEX-8400 Gateway

Первый этап шлюза HiQSDR -> Flex SmartSDR API.

## Структура

- `FlexControlServer` - discovery, TCP-команды Flex и состояние Slice A;
- `RadioBackend` - интерфейс управления приёмником и источника IQ;
- `TestRadioBackend` - тестовая реализация backend с генератором IQ;
- `TestIqSource` - временный генератор IQ вместо оборудования;
- `VitaPacketBuilder` - создание пакетов VITA-49;
- `VitaUdpStreamer` - отправка VITA-49 клиенту;
- `Flex8400Emulator` - координация компонентов.

При подключении HiQSDR будет заменён `TestRadioBackend` на `HiqSdrBackend`.
Слои Flex API, VITA-49 и UDP-передачи останутся без изменений. Карта
поддержки команд и соответствия HiQSDR приведена в `docs/COMMAND_MAPPING.md`.

Сейчас проект эмулирует FLEX-8400 для AetherSDR:

- отправляет discovery-пакет SmartSDR на UDP/4992;
- принимает TCP-соединения на порту 4992;
- реализует стартовый обмен `V`, `H`, `C`, `R`, `S`;
- создаёт один slice и один panadapter на 14.100 MHz;
- передаёт VITA-49 FFT-поток с тестовым белым шумом около -73 dBm и waterfall
  с шумовым фоном и тестовой несущей;
- передаёт VITA-49 stereo RX-аудио с тестовым тоном 700 Hz.

HiQSDR в этот этап ещё не включён. Следующая задача - добавить `HiqSdrClient`, который будет принимать IQ по UDP от устройства и преобразовывать его в VITA-49 для AetherSDR.

## Тестовый сигнал

Внутренний IQ-источник содержит несущую на `14.125000 MHz`. В режиме USB
приёмный звук появляется, когда Slice A настроен в диапазон `14.122100` -
`14.124900 MHz`. Например, при `14.124300 MHz` слышен тон около 700 Hz.

## Сборка

```sh
cmake -S . -B build
cmake --build build
./build/hiqsdr-flex6xxx
```

Для подключения с другой машины укажите адрес интерфейса:

```sh
./build/hiqsdr-flex6xxx --address 192.168.1.10
```
