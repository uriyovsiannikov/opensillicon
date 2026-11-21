# Nouveau: начало порта NVKM

Первая аппаратная цель — **GeForce GTX 1050 Ti / Pascal GP107 (NV137)**.
Реализованы распознавание GPU, чтение VBIOS/DCB и начальное чтение EDID по
I2C/AUX. Добавлены чтение состояния дисплея и подготовка блока таймингов.
Добавлена подготовка instance/RAMHT/DMA-объектов в памяти ОС.
Передача управления дисплеем, установка режима и ускорение ещё не реализованы.

## Реализовано

- `device.c`: перенесены алгоритм распознавания `PMC_BOOT_0`, таблица 94
  идентификаторов и определение частоты кварца из NVKM Linux **v6.12**.
  Наличие имени чипа в таблице означает только возможность распознавания.
- `pci.c`: подключение к штатным PCI claims и MMIO leases OpenSillicon,
  проверка BAR0, отображения только для чтения двух необходимых окон,
  чтение `PMC_BOOT_0`, `PMC_BOOT_1`, `PEXTDEV_BOOT_0`, освобождение ресурсов.
- Диагностика при загрузке после основной PCI enumeration. Выводятся BDF,
  PCI ID, имя чипа, BOOT0, адрес/размер BAR0 и частота кварца.
  У GP107 дополнительно выводится `[GP107 target; probe only]`.
- Опция командной строки ядра `nouveau=off` полностью отключает этот проход.
- `bios_shadow.c`: для GP107 перенесено получение VBIOS из VRAM через
  PRAMIN по алгоритму `nvkm/subdev/bios/shadowramin.c`. Проверяются display
  fuse `0x021c04` и указатель VRAM `0x619f04`, затем временно меняется
  `0x001700`. Отображение `0x700000..0x7fffff` доступно только для чтения.
  Прежнее значение окна восстанавливается с read-back до разбора таблиц.
- При отказе PRAMIN автоматически пробуется PROM, как в NVKM: только чтение
  BAR0 `0x300000..0x3fffff` после временного сброса бита ROM shadow в
  BAR0 `0x088050` (`gp100_pci` → `nv40_pci` → регистр PCI `0x50`).
  Полное исходное значение shadow-регистра восстанавливается с read-back
  до разбора ROM и при любом отказе. Expansion-ROM BAR не включается,
  запись flash и исполнение её содержимого не выполняются. В журнале
  отдельно указаны отказ PRAMIN, выбранный PROM и его диагностика.
- `bios.c`: проверяются ROM/PCIR, цепочка образов и NPDE; перенесены
  логическая адресация image-D, поиск BIT/BMP, версия VBIOS, DCB v3.0–4.1,
  выходы и таблица разъёмов v3.0/v4.0. Выводятся connector type, HPD ID,
  маски heads/OR, I2C index, link и максимальные параметры DP из DCB.
- `nouveau=nobios` оставляет только распознавание GPU, уже проверенное
  на настоящей GTX 1050 Ti. Ошибки VBIOS/DCB выводятся в журнал и не мешают
  продолжению загрузки.
- `bios.c` разбирает CCB v3.0–4.1: индекс из DCB преобразуется в отдельные
  номера drive, AUX и shared PAD. Отсутствующая/повреждённая CCB запрещает
  EDID-пробу, сохраняя результат VBIOS/DCB.
- `ddc.c`: GPIO I2C GF119 для HDMI/DVI, включая ACK/NACK, repeated START,
  clock stretching, STOP и E-DDC segment pointer; I2C-over-AUX GM200 для DP,
  включая владение контроллером, MOT, DEFER, короткие/пустые ответы и STOP.
  Используется только `0x50` для EDID и `0x30` для segment pointer.
- `ddc_pci.c` отображает страницу BAR0 `0xD000..0xDFFF`. PAD mux/power,
  управление I2C/AUX и autodpcd восстанавливаются после пробы. Настройки
  транзакции/передачи AUX сохраняются без повторного запуска старой команды;
  volatile status/RX data не восстанавливаются. Ожидания ограничены,
  бюджет задержек — 200 мс на попытку плюс отдельная ограниченная очистка.
  Перед пробой проверяется наличие часов с достаточным разрешением.
- `edid.c`: EDID 1.x, manufacturer/product/name, базовые detailed/standard/
  established timings, CTA detailed timings и распространённые VIC
  (включая 1080p/2160p). До четырёх блоков EDID и 32 режимов. Проверяются
  checksum и длины, повреждённые расширения исключаются с диагностикой.
  DisplayID, полный набор CTA/VIC и мониторные quirks пока не реализованы.
  Выводятся строки `EDID`, `monitor`, `mode` с частотой и признаком preferred.
- `nouveau=noedid` отключает только I2C/AUX/EDID и сохраняет VBIOS/DCB.
  Неподключённый или не отвечающий монитор не мешает загрузке. Неудачное
  восстановление записанных управляющих битов считается ошибкой ядра.
- EDID DTD теперь сохраняет полные hsync/vsync/total и полярности. Режимы
  без точного progressive DTD остаются только в списке. Повреждённый
  дубликат DTD не может заменить clock, оставив старые координаты sync.
- `display.c`: перенесено чтение armed/async head/SOR состояния GF119,
  динамическая маршрутизация GM200 и расположение core descriptor GP102
  (`0x611494`, в отличие от старого `0x610494`). Читаются размеры растра,
  sync/blank, clock, scanout format/storage/DMA, scan position и supervisor.
- Планировщик переводит exact DTD в 24 DWORD блока таймингов NV907D,
  используемых GP102 core `0x987d` через `head917d`. Проверяются реальная
  OR→SOR→head маршрутизация, TMDS protocol/link, границы полей и соответствие
  текущему 32-битному RGB framebuffer. DP, смена размера framebuffer,
  interlaced/composite/stereo и неоднозначная маршрутизация пока отклоняются.
- `display_pci.c` читает состояние через **read-only** MMIO leases; выводит
  `DISP` и `MODEPLAN`. Блок команд создаётся только в памяти CPU: канал не
  активируется, `UPDATE` не отправляется. Для применения ещё нужны собственные
  instance memory/RAMHT, surface DMA context, notifier и supervisor/PLL.
  Это подготовка к modesetting, а не готовая транзакция или takeover.
- `nouveau=nodisplay` отключает новый захват состояния/планирование,
  сохраняя VBIOS/DCB/EDID. При `nouveau=noedid` он тоже не выполняется.

После загрузки журнал доступен командой `cat /tmp/btlog.log`.

Для VBIOS временно записывается регистр адреса окна PRAMIN и, при PROM
fallback, регистр ROM shadow; режим дисплея не меняется.
Определение размера BAR использует существующий
`pci_bar_probe_size`: временно отключает PCI decode/mastering,
пишет маску размера BAR, восстанавливает и проверяет конфигурацию.
Проба не включает memory decode у отключённого устройства, DMA, IRQ,
исполнение VBIOS, firmware или изменение видеорежима. Текущий framebuffer
Limine остаётся источником изображения. Нет постоянного владения устройством;
API пробы предназначен только для последовательного вызова при загрузке.
Если восстановление окна или cleanup MMIO/claim не удаётся, ядро
останавливается с ошибкой в журнале.

Проба требует BAR0 размером как минимум `0x101004` байт и включённого PCI
memory decode. Big-endian GPU отклоняется без попытки переключить порядок
байтов; vGPU Turing+ и неизвестные чипы отклоняются. GA100 (NV170) остаётся
исключён из списка по умолчанию, как в выбранной версии upstream.

Получение VBIOS требует BAR0 от 8 МиБ, включённого display и доступной копии
BIOS в VRAM. Ограничения snapshot: до 1 МиБ, до 64 образов, PCIR/NPDE-заголовки
в первых 4 КиБ каждого образа. Таблицы ограничены 32 выходами и 32 разъёмами;
повреждённые границы, размеры, ссылки и неизвестные версии отклоняются.
Как в `shadow_image` upstream NVKM, допускается корректный префикс ROM,
если следующий образ недоступен в VRAM. Такой snapshot помечается
`chain=partial (valid prefix only)`; данные недочитанного образа не попадают
в парсер и не могут удовлетворять ссылкам DCB/BIT. Отсутствие любого
полностью прочитанного образа остаётся ошибкой. Vendor проверяется для
первого образа, а не для внутренних расширений цепочки.
Как и у PRAMIN-источника NVKM, несовпадение контрольной суммы изменяемой
VRAM-копии диагностируется, но само по себе не запрещает чтение DCB.
PROM fallback требует BAR0 от 4 МиБ. PCI ROM/ACPI fallback пока отсутствуют.

DCB описывает возможности платы. HPD в выводе — идентификатор линии из
прошивки, а не текущее состояние подключения. Успешное чтение EDID относится
к моменту загрузочной пробы; hotplug пока не обслуживается. Исполнение VBIOS
init-скриптов, DPCD/link training и переключение видеорежимов не выполняются.
Выходы с внешним bridge и старые типы шин NV04/NV4E пока пропускаются.

## Источник и лицензия

Неизменённый исходник для сверки находится в `upstream/device-base.c`.
Он **не компилируется**: полный NVKM ещё не перенесён. Источник:

- [Linux v6.12, nvkm/engine/device/base.c](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/engine/device/base.c)
- [Таблица имён Nouveau](https://nouveau.freedesktop.org/CodeNames.html)

Контрольная сумма сохранена в `upstream/SHA256SUMS`. Исходник и адаптированный
`device.c` сохраняют разрешительную лицензию MIT и copyright upstream.
Исходники BIOS из той же версии Linux сохранены в `upstream/bios/`, с отдельным
`SHA256SUMS`; они не компилируются. Адаптации `bios.c`/`bios_shadow.c` сохраняют
MIT copyright/лицензию. Основные источники:

- [PRAMIN shadow](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/subdev/bios/shadowramin.c)
- [PROM shadow](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/subdev/bios/shadowrom.c)
- [DCB](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/subdev/bios/dcb.c)
- [Connector table](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/subdev/bios/conn.c)

CCB и I2C/AUX источники сохранены в `upstream/i2c/` с `SHA256SUMS`;
адаптация транспорта `ddc.c` сохраняет MIT-лицензию upstream. Источники:

- [CCB](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/subdev/bios/i2c.c)
- [GF119 I2C](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/subdev/i2c/busgf119.c)
- [GM200 AUX](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/subdev/i2c/auxgm200.c)
- [GM200 PAD](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/subdev/i2c/padgm200.c)
- [EDID formats/mode decoding reference](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/drm_edid.c)

Дисплейные исходники и определения методов сохранены в `upstream/display/`
с `SHA256SUMS`. Адаптация `display.c` сохраняет MIT-лицензию upstream;
PCI-обвязка — BSD-2-Clause. Основные источники:

- [GP102 core/channel](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/engine/disp/gp102.c)
- [GF119 head/SOR/supervisor](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/engine/disp/gf119.c)
- [GM200 SOR routes](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/nvkm/engine/disp/gm200.c)
- [Timing conversion](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/dispnv50/head.c)
- [Head timing methods](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/gpu/drm/nouveau/dispnv50/head907d.c)

Нативный PCI-слой — BSD-2-Clause. LinuxKPI с фиктивно успешными реализациями
операций для этого этапа не требуется.

## Проверка

```sh
make kernel
make check-nouveau
# При установленных runtime-библиотеках санитайзеров:
NOUVEAU_SANITIZE=1 make check-nouveau
# Можно выбрать Clang, если runtime санитайзеров GCC отсутствует:
CC=clang NOUVEAU_SANITIZE=1 python3 src/tools/checks/nouveau.py
```

Host-тесты проверяют GP107, старые чипы, неизвестные значения, NV170 opt-in,
частоты кварца, фильтрацию PCI, 32/64-битный BAR, маленький/неверный BAR,
отказы claim/map/address, big-endian/vGPU и cleanup во всех ветках возврата.
Отдельные BIOS-тесты проверяют PCIR/NPDE, цепочку образов, image-D translation,
BIT/BMP и версию, DCB/connector references, checksum warning, отказы allocation,
map/address, возврат PRAMIN в исходное состояние и 12000 мутаций ROM с
точными границами буферов (для ASan/UBSan).

EDID-тест моделирует EEPROM на линиях GPIO I2C и GM200 AUX-контроллер:
проверяются данные блоков, ACK/NACK, stretching/timeout, отсутствие sink,
DEFER, короткие/пустые ответы, занятый контроллер, E-DDC сегменты,
восстановление регистров и обнаружение отказа восстановления. Парсер EDID
проверяется на известных DTD/CTA/standard/interlaced режимах и 10000
мутаций буферов точного размера. Это host-модели, а не аппаратные тесты.

Display-тесты сверяют все 24 DWORD с фиксированным ожидаемым блоком
1920x1080p60, физический OR и логический SOR/link (включая несовпадение
номеров), dual-link, armed/async, ADJ1000DIV1001, отсутствие маршрута,
неправильные тайминги/формат/размер framebuffer, отказ каждого read/map/address,
освобождение частично созданных leases и 12000 мутаций аргументов планировщика.
Проверяется, что native capture отображает только read-only ресурсы и не
меняет ни одного байта модели BAR. Это не проверяет применение режима GPU.

Распознавание GP107 подтверждено пользователем на настоящей GTX 1050 Ti:
PCI `10de:1c82`, BOOT0 `137000a1`, BAR0 `a3000000`, 16 МиБ, кварц 27 МГц;
система продолжила загрузку до оболочки (2026-11-21).
VBIOS/DCB также подтверждены на этой карте: PRAMIN возвращает блок с
сигнатурой `0xBEEF`, после чего PROM успешно предоставляет 234496 байт,
5 образов, полную цепочку и корректную legacy checksum. Версия прошивки
`86.07.39.80.0e`, DCB v4.1: 4 записи выходов для HDMI, DP (DP/TMDS на одном
разъёме) и DVI-D. В таблице 16 connector slots; это не число физических портов.
Чтение EDID по I2C также подтверждено аппаратно: список включает
1920x1080p74.972, 1920x1080p50 и другие режимы. Успешное чтение по DP/AUX
пока не подтверждено. Display snapshot и timing block подтверждены следующим
фото: HEAD0/SOR1, link A, armed=async, 1920x1080p60, totals 2200x1125,
148.5 МГц, 24 DWORD построены без отправки. Core-head scanout поля нулевые;
это ещё не определяет расположение поверхности base-channel.
Framebuffer Limine продолжает вывод изображения.

Для аппаратной проверки собрать `make live-iso`, загрузиться и посмотреть
`cat /tmp/btlog.log`. Ожидаются строки `VBIOS PRAMIN validated` или
`VBIOS PROM validated`, `ROM bytes=`,
версия и `DCB v41 outputs=... connectors=...` с перечнем выходов. При ошибке
нужны строки `VBIOS`/`ROM`/`DCB`, включая `ROM capture`, `ROM PCIR`,
`ROM declared_bytes`, `ROM header+00/+10` и fuse/pointer/window. Если зависает,
загрузиться с `nouveau=nobios` (или `nouveau=off` для отключения всей пробы).
Для EDID нужны строки `CCB`, `EDID`, `monitor` и `mode`; при зависании
нового этапа сначала использовать `nouveau=noedid`.
Для подготовки modesetting нужны строки `DISP` и `MODEPLAN`: head/SOR,
ROUTE, armed/async растр, scanout и точные DTD-тайминги. Если этот проход
мешает загрузке, использовать `nouveau=nodisplay`. Надпись
`timing block built (not submitted)` означает только успешное кодирование.

## Instance memory и ресурсы дисплея

`memory.c` переносит алгоритмы NVKM v6.12 `core/ramht.c`,
`engine/dma/usergf119.c`, `subdev/fb/gp102.c` и `ramgp102.c`.
Оригиналы с лицензиями и SHA256 находятся в `upstream/memory/`.
Формируется CPU-образ instance memory 64 КиБ: RAMHT 4 КиБ / 512 slots,
два 24-байтных VRAM DMA-объекта для surface и notifier, область notifier
4 КиБ. Handles привязываются к core channel 0 и base channel head+1.
Hash folding, channel bits, линейное разрешение коллизий и object offsets
соответствуют GF119 display. Surface context описывает адресное пространство
VRAM; это не выделение поверхности и не определение адреса текущего scanout.

До relocation notifier содержит относительные адреса. Отдельная функция
relocation требует диапазон VRAM, которым вызывающий код уже владеет;
проверяет 64-КиБ alignment, 40-битную адресацию, размер и исключает первые
256 КиБ / последний 1 МиБ. Она не является VRAM allocator и в boot probe
не вызывается. Адрес host DMA нельзя использовать как VRAM instance.

`memory_dma.c` выделяет штатным DMA API staging image 64 КиБ и HOST push
4 КиБ, проверяет адреса/владение CPU, копирует 24 DWORD таймингов и кодирует
HOST descriptor. При ошибке освобождаются частичные allocations; при отказе
освобождения handles сохраняются, native probe останавливает ядро. Успешная
boot probe также освобождает оба буфера. Ни `dma_device_acquire`, ни загрузка
instance в VRAM, ни привязка каналов, ни UPDATE пока не выполняются.

Добавлено чтение GP102 capacity register 0x100ce0 и обеих банков base-channel
surfaces GF119 для каждого head, включая context DMA handles. `BASE offset`
пока context-relative (единица 256 байт), а не подтверждённый физический
адрес VRAM. Число `MEM total_bytes` означает ёмкость, а не свободную память.
Проверка на настоящей GTX 1050 Ti требуется для новых строк `BASE` и `MEM`.

Host-тесты проверяют эталонные DMA DWORD, ёмкость 4 ГиБ и режим 15/16,
RAMHT collisions/wrap/full, независимость channels, границы relocation,
CPU ownership, отказы allocation/query, неверные DMA addresses, освобождение
и сохранение handles при cleanup failure. Display-тест покрывает последний
head/вторую поверхность и отказ всех восьми read-only mappings.

## Boot framebuffer и BAR1

`BOOTFB` выводит kernel-only физический CPU-адрес framebuffer из Limine HHDM,
размер, pitch и bpp. Под активным PCI claim проверяется размер BAR1 и полное
вхождение boot framebuffer в его aperture. Отображение BAR1 не создаётся.
Offset в BAR1 не считается физическим адресом VRAM: для Pascal нужна
трансляция через GPU MMU. Пограничные spans/overflow проверяются host-тестом.

Фото пользователя от 2026-11-21 подтвердило создание и освобождение CPU
staging на GTX 1050 Ti (`HOST=0x3403`). У base channel 0 `ctrl=0x40000088`,
`push=1`; у обеих surfaces DMA/offset/size/storage нулевые, params=0xe900.
По этим полям адрес действующего GOP framebuffer ещё не определён.

## Чтение VRAM через PRAMIN

`vram.c` переносит read transport `nv50_instobj_rd32_slow` из NVKM v6.12
`subdev/instmem/nv50.c`. Чтение ограничено 64 КиБ за вызов, проверяются
40-битные адреса, границы ёмкости и выравнивание. Переход через границу
1-МиБ окна поддерживается. Единственная запись — регистр окна `0x1700`;
исходное значение восстанавливается с read-back при любом выходе после
попытки изменения. Ошибка восстановления требует остановки native probe.
Caller обязан держать исключительный PCI claim и не менять окно параллельно.

`vram_pci.c` исследует 64-КиБ firmware display instance, если `0x610010`
соответствует известному формату VRAM/64-КиБ alignment. PRAMIN aperture
`0x700000..0x7fffff` отображается только для чтения; `0x1700` — writable.
До разбора памяти прошивки окно уже восстановлено. В журнале `FWINST`
показывает адрес/первые DWORD, `FWCTX` ищет ненулевые context handles из
снимка дисплея в известном 4-КиБ RAMHT layout. Если firmware использует иной
layout, поиск не определяет объект; это не доказательство отсутствия DMA.
Никакие firmware-объекты не исполняются и не связываются с каналами.

Тест PRAMIN моделирует пересечение окна, верхнюю границу 40-битного адреса,
отказ каждого IO, в том числе уже выполненную запись с ошибкой callback,
неверный read-back и отказ восстановления. Native tests проверяют mapping,
allocation, cleanup, исходное значение окна и неизменность VRAM aperture.
Новый аппаратный проход требует строк `BOOTFB`, `FWINST`, `FWCTX`, `MEM`.
Эти строки не подтверждают передачу управления дисплеем.

## Следующие этапы для GP107

1. Определить фактический UEFI scanout/base DMA context, реализовать
   владение VRAM и загрузку instance/RAMHT/notifier в выделенный диапазон.
2. Минимальный NVKM lifecycle, таймеры, прерывания и управление ресурсами;
   перенос зависимостей `gp102_disp_new`, используемого GP107.
3. Управление дисплеем Pascal: heads, SOR, HDMI/DP, vblank, modesetting,
   передача scanout из загрузочного framebuffer.
4. Память GPU: instance memory, BAR, VRAM/GTT, `gp100_mmu`, буферные объекты
   и отображения в процессах; затем FIFO, fences и синхронизация.
5. Подписанные firmware и ACR/SEC2/PMU, `gp107_gr`, отправка команд и
   пользовательский интерфейс для Mesa/libdrm Nouveau.

Полный DRM/GEM/TTM/KMS, `/dev/dri`, ускорение 2D/3D, reclocking и управление
питанием на этом этапе отсутствуют. Порядок выше — план переноса, а не
утверждение о готовности этих подсистем.

## GOP на второй PCI aperture

Фото пользователя: CPU framebuffer `0xa1000000`, 8294400 байт, pitch 7680,
32 bpp; BAR1 `0x90000000`, 256 МиБ, не содержит framebuffer. Display instance
`0x610010=1` не соответствует VRAM instance NVKM (`...09`), поэтому чтение
FWINST было пропущено. Состояние base surfaces по-прежнему нулевое.

Проверяются обе PCI memory apertures GP107: BAR1 и BAR3. В NVKM
`subdev/bar/gf100.c` вторая называется BAR2, но создаётся по PCI index 3;
индексы 2/4 — старшие половины 64-битных BAR. Отказ sizing BAR1 не мешает
проверке BAR3. Полный span framebuffer должен находиться внутри aperture.
`BARMAP` читает, не меняя, BAR VM instance pointers 0x1704/0x1714 и status
0x1710 по `gf100.c`/`gm107.c`. Даже совпадение BAR3 не доказывает прямую
адресацию VRAM: для этого нужна проверка GPU page tables.

`BARVM` исследует VM instance BAR1/BAR3 отдельно от display instance. По
известному формату gf100_bar включённый регистр задаёт 4-КиБ VRAM instance;
bit 30 разрешён только для второй aperture (halve). Неизвестные верхние bits
и выход за VRAM запрещают чтение. После восстановления PRAMIN журнал
показывает raw PDB descriptor по instance+0x200 и VA limit по +0x208 из
`gf100_vmm_join_`. Эти данные ещё не являются переводом адреса framebuffer.
Формат page tables (legacy или Pascal VER2) пока не предполагается.
Тесты покрывают BARVM при display instance=1, disabled/unknown formats,
halve, недопустимый диапазон и partial mapping/allocation cleanup.

Фото подтвердило PCI BAR3 `0xa0000000`, 32 МиБ, framebuffer offset 16 МиБ;
BAR1 context=0, BAR3 context=0x40000000. В опубликованном NVIDIA GM107
`dev_bus.h` бит 31 — MODE (0 physical / 1 virtual), бит 30 — DEBUG_CYA.
Поэтому ранняя надпись disabled/unsupported была неточной: это physical
mode без VM instance. Эти регистры не следует интерпретировать как PDB.
Источник: NVIDIA open-gpu-kernel-modules tag 570.124.04,
`src/common/inc/swref/published/maxwell/gm107/dev_bus.h`;
`kern_bus_gm107.c` также описывает физический bootstrap BAR2.

## Проверка кандидатов адреса GOP framebuffer

`FBMATCH` работает только для наблюдаемой конфигурации GP107: PCI BAR3
32 МиБ, context=0x40000000, framebuffer полностью в верхних 16 МиБ.
Без записи VRAM сравниваются 128 выборок по 64 байта, распределённых по
всему span framebuffer, включая последние 64 байта. Проверяются три
гипотезы: VRAM 0, VRAM offset BAR3 и VRAM size-32MiB+offset.
Это гипотезы для диагностики, а не формула трансляции physical BAR3.

Каждая GPU-выборка читается через PRAMIN; окно восстанавливается перед
повторным CPU-чтением тех же пикселей. Изменившиеся пиксели дают unstable,
однотонные данные — inconclusive. Только совпадающие неоднородные выборки
дают sample match. Совпадение не доказывает соответствие всего диапазона,
активный scanout или владение VRAM. Регистры BAR mode и поверхность не
изменяются; PRAMIN register восстанавливается даже при отказе чтения.

Новый kernel-only framebuffer API читает до 16 DWORD из действующей Limine
aperture с проверкой выравнивания и границ. Пользовательский framebuffer ABI
не меняется. Host-тесты проверяют match/mismatch на последней выборке,
flat/unstable, отказ каждого CPU/GPU-чтения, недопустимые диапазоны и
частичный native mapping cleanup. Аппаратная проверка ещё требуется:
нужны строки `FBMATCH` и `BARVM` из нового ISO.

## Surface context для найденного кандидата

Аппаратный результат: неоднородные выборки совпали только с VRAM 0;
VRAM 16 МиБ и VRAM 0xff000000 не совпали. Это соответствует предполагаемому
расположению boot surface в начале VRAM, но остаётся выборочной проверкой.
`FBMATCH` передаёт кандидата дальше только при ровно одном совпадении.

`nouveau_display_surface_plan` кодирует 9 DWORD по `head907d_core_set`:
HEAD_SET_OFFSET, SIZE/STORAGE/PARAMS/CONTEXT_DMAS_ISO, VIEWPORT_POINT_IN.
Поддерживается pitch layout 32bpp A8R8G8B8/A8B8G8R8 с проверкой pitch,
выравнивания 256 байт, 16-битных размеров и полного span внутри VRAM.
Контекст предполагается VRAM [0,size), offset задаётся относительно него.
Host-тест сверяет точные DWORD для 1920x1080/7680-byte pitch/address 0,
последний head, RGB/BGR и отказы границ/формата. `SURFACEPLAN` означает
только CPU-кодирование: оно не привязывает context, не выделяет поверхность,
не отправляет UPDATE и не даёт владения участком VRAM.

После единственного sample match surface methods добавляются к timing block
в существующем HOST push buffer: 24+9=33 DWORD. Перед добавлением проверяются
CPU ownership обоих DMA allocations, HOST descriptor, RAMHT handle core
channel и точное содержимое VRAM surface DMA object. Повторное добавление
запрещено; отказ до записи оставляет timing block и staged_words прежними.
Notifier по-прежнему instance-relative, UPDATE/активация каналов отсутствуют.
Оба буфера освобождаются в конце пробы. Тест сверяет весь добавленный блок,
сохранение таймингов/нулевого хвоста, query/ownership/descriptor отказы.
Аппаратный следующий результат: `SURFACEPLAN` и
`MEM surface appended; staged_words=33 (CPU owned, not submitted)`.

## Учёт диапазонов VRAM

`vram_pool.c` реализует native first-fit allocator с 32 слотами для ranges,
резервов и allocations. Базовые исключения GP102: VGA первые 256 КиБ,
VBIOS последний 1 МиБ. Caller добавляет scanout и другие известные резервы;
перекрывающиеся исключения допустимы, резерв поверх live allocation запрещён.
Размер/выравнивание проверяются без overflow, поиск ограничен числом слотов.
Handles содержат pool/slot/generation: double-free и stale handle после
повторного использования слота отклоняются; wrap generation закрывает
дальнейшее выделение. Pool непереносим, caller сериализует доступ.

Это software accounting, не доказательство свободной VRAM. Инициализация
pool не отключает прошивку и не передаёт владение GPU. До реализации
аппаратного lifecycle загружать данные по выбранному адресу нельзя.
Тест проверяет scanout/VGA/VBIOS exclusions, overlap, alignment, exhaustion,
неизменность при invalid init, stale handles и 240000 случайных allocations.

## План размещения instance и notifier

`nouveau_display_staging_place` выделяет в range pool логический диапазон
64 КиБ / alignment 64 КиБ и кодирует в CPU-образе абсолютный VRAM notifier
(start=placement+0xf000, limit=placement+0xffff). Подготавливается значение
instance register; оно никуда не записывается. Проверяются CPU ownership
обоих DMA allocations и поверхность DMA context полного диапазона VRAM.
При ошибке до relocation CPU-образ не меняется, после неудачи allocation
диапазон освобождается. Повторное размещение запрещено.

Проба использует консервативные дополнительные исключения: первые 64 МиБ
(загрузочная поверхность и наблюдавшиеся firmware workspace/VBIOS pointers)
и последние 16 МиБ. Для 4-ГиБ GP107 логический instance начинается с
0x04000000, register=0x00040009, notifier=0x0400f000..0x0400ffff.
`VRAMPLAN` явно обозначает логический план без upload/hardware ownership.
Этот план нельзя принимать за карту всех allocations прошивки.

Range handle живёт вместе с staging: release сначала освобождает CPU DMA
buffers и только потом снимает логический резерв. Если освобождение любого
буфера не удалось, placement handle сохраняется до повторного cleanup.
Тесты сверяют relocation DWORD/register, отказы query, запрет повторного
размещения и сохранение/снятие резерва при частичном release failure.
Фото предыдущего этапа подтвердило `SURFACEPLAN` и `staged_words=33` на
GTX 1050 Ti; новый VRAMPLAN пока проверен только host-тестами.

## Lifecycle DMA-каналов GP102

`channel.c` переносит GP102 init и GF119 fini core/base/overlay DMA channels
(0..8) из NVKM v6.12. Поддержаны HOST push descriptor, GP102 0x61149x,
PUT=0 при запуске и отдельные core/base stop masks. PIO не реализован.
Транспорт read/write/time/delay/resource acquire/release инъецируется:
в native boot probe данный lifecycle пока не вызывается.

Начало требует inactive channel и resource acquire callback, который обязан
уже обеспечить владение дисплеем, загрузку instance/RAMHT/notifier и DMA
push ownership. Сам range allocator этого не обеспечивает. Чужой активный
канал отклоняется без записей. Любая ошибка после acquire (включая callback
write=false после реальной записи) оставляет канал quarantined и ресурсы
закреплёнными. Остановка повторяема; release разрешён только после
подтверждённой неактивности и чтения PUT. Ошибка release сохраняет handles.

Ожидание ограничено 2 секундами и 20001 чтением; stalled/backward clock,
IO failure и all-ones read приводят к отказу. Fini дополнительно требует
снятия GP102 busy bit, чтобы не освободить память после неудачного init.
Никакие staged методы, UPDATE, supervisor interrupts или engine handoff
этот модуль автоматически не отправляет.
Host-тест сверяет полный init protocol, все 9 channel IDs, core/base stop,
каждый read/write/time отказ, failed writes с side effect, retry/quarantine,
resource-release failure и timeout/stalled/backward clock. Реальное
железо ещё не выполняло этот код; потребуется integration с engine lifecycle.

`nouveau_channel_kick` добавляет явную публикацию PUT для immutable push
buffer. Prepared byte count фиксируется до acquire; PUT может только
расти, быть кратным 4 и не превышать подготовленные байты или 4092.
Ring wrap и редактирование CPU при device ownership не поддержаны.
Doorbell write и read-back проверяются: ошибка делает исполнение
неопределённым и оставляет quarantine, повторная отправка запрещается.
Тесты проверяют 33-DWORD block / PUT=132, границы prepared span, duplicate/
backward/misaligned PUT, отказ write/read, read-back mismatch и последующую
остановку. Этот API может исполнять методы GPU и в boot probe не вызывается.

## Транзакция загрузки instance

`upload.c` сохраняет исходный диапазон (до 64 КиБ), записывает CPU-образ и
сверяет все DWORD чтением обратно. Любой отказ/несовпадение вызывает полное
восстановление backup и повторную сверку. При failed write, который уже
дошёл до hardware, rollback всё равно выполняется. Отказ восстановления
или потеря владения оставляет quarantine: range handle и backup сохраняются,
finish запрещён. Восстановление повторяемо после восстановления транспорта.

IO включает обязательную owns-проверку настоящего владения VRAM/engine;
software pool недостаточен. После save ответственность за allocation
переходит транзакции: caller не должен отдельно освобождать handle.
Backup, pool и IO должны жить до finish, CPU image не должен пересекаться
с backup. Pin делается до hardware binding; restore/finish при pin запрещены.
Unpin допустим только после аппаратной отвязки и остановки всех потребителей.
Сама транзакция не записывает display instance register и не запускает канал.

Host-тест проверяет весь 64-КиБ образ, каждый word failure на малой транзакции,
потерянные/искажённые записи, ошибки read-back/rollback, retry/quarantine,
потерю ownership, aliasing backup и pin/unpin. Транспорт до native hardware
пока не подключён, загрузочная проба по-прежнему не пишет VRAM.

`nouveau_vram_write` добавляет low-level PRAMIN write по алгоритму
`nv50_instobj_wr32_slow`. Общая read/write передача ограничена 64 КиБ,
проверяет выравнивание/40-битные границы и восстанавливает 0x1700 с read-back
после любой попытки смены окна, включая failed writes с side effect.
Write требует отдельного writable aperture; имеющийся диагностический
native transport по-прежнему read-only и aperture writes отклоняет.

Low-level write не сохраняет VRAM и не проверяет содержимое: этим занимается
upload transaction. Host-тест объединяет транзакцию и PRAMIN, проверяет
полный 64-КиБ write/read, каждый write failure, rollback исходных данных и
восстановление окна. Native adapter обязан считать PRAMIN_RESTORE_FAILED
фатальной ошибкой транспорта, а не понижать её до обычного отказа чтения.
Новый write API из boot probe не вызывается.

Интеграционный `nouveau_lifecycle.c` связывает upload pin/unpin с resource
callbacks канала: подготовленный image остаётся закреплённым при каждом
отказе start-write, ambiguous PUT и stop timeout. Пока channel держит
ресурс, restore/finish запрещены. После verified stop/release выполняются
restore, сверка исходных bytes и освобождение range. Потеря реального
ownership при release также удерживает pin до восстановления владельца.
Это host-модель цепочки, не native engine handoff и не hardware validation.

## GF119 firmware handoff и привязка instance

`engine.c` переносит request/ack handoff и display instance binding из
`gf119_disp_init`. До изменений проверяются все 17 GP102 channel controls:
активный/ожидающий канал даёт BUSY без записей. Сохраняются instance,
firmware control и три interrupt masks. При pending supervisor bit 0x100
выполняются W1C acknowledgement и снятие firmware request bit; ожидание
ack ограничено 2 секундами/20001 чтением. Все IRQ остаются выключенными:
supervisor/PLL ещё не интегрированы.

Bind принимает только проверенный upload 64 КиБ с alignment 64 КиБ,
закрепляет его ДО потенциально неудачной записи 0x610010 и сверяет read-back.
Restore требует inactive channels, возвращает старый instance с read-back,
снимает pin и восстанавливает исходные VRAM bytes. Только после успешного
rollback возвращаются IRQ masks и firmware request bit. Если rollback не
удался, память удерживается в quarantine, прошивке управление не возвращается.
W1C status acknowledgement необратим: старое pending event не воспроизводится.
Это не полное восстановление всех внутренних состояний firmware.

Host-тест проверяет полный handoff/bind/restore, каждый take read/write failure,
failed bind с side effect, busy channel всех 17 номеров, stuck/stalled/backward
clock, отказ VRAM rollback и retry. Нативная boot probe этот API пока не
вызывает: capability copying, IRQ/supervisor/PLL и физический adapter остаются
незавершённой частью engine integration.

`nouveau_engine_capabilities` добавляет head/SOR copying из gf119_disp_init.
По head count/mask проверяется до четырёх heads; копируются три CRTC caps
на head и фиксированный банк восьми SOR caps. GP102 не объявляет DAC engine,
поэтому legacy DAC registers не затрагиваются. До первой записи сохраняются
все прежние зеркала; каждая новая запись проверяется read-back. Bind запрещён
до успешной подготовки caps. При rollback зеркала восстанавливаются до
возврата IRQ masks и firmware request; любой отказ оставляет quarantine.

Тесты проверяют все capability и restore register read/write failures,
частичный rollback и повторное восстановление, а также неверный read-back
instance после фактической записи. Supervisor/PLL, IRQ dispatch и native
adapter всё ещё не подключены; boot probe handoff не выполняет.

## GF119 supervisor dispatcher

`supervisor.c` переносит порядок `gf119_disp_super`: stage 1 делает armed/async
snapshot и detach1; stage 2 выполняет detach2 для всех отмеченных heads,
затем PLL для mask 0x10000, затем attach2; stage 3 делает attach3.
Head masks читаются до W1C ack pending stage. После успешных callbacks
очищаются head masks и записывается supervisor completion 0x80000000.

Callbacks обязаны реализовывать реальные IED/routing/PLL действия; заглушки
с возвратом успеха недопустимы. Ошибка read/write/action оставляет FAILED,
completion не отправляется. Автоматический retry и reentry запрещены,
поскольку BIOS-действия и W1C могут уже иметь side effects. Несколько
одновременных stage bits отклоняются до ack. Неизвестные status bits
остаются нетронутыми. Это polling primitive, не готовый IRQ dispatcher.
Host-тест сверяет порядок всех стадий и все read/write/action отказы.
Нативная probe supervisor не вызывает, IED interpreter пока отсутствует.

## Fractional VPLL

`pll.c` переносит fractional path gt215_pll_calc, используемый
`gf100_devinit_pll_set` через gm200 devinit GP107. Декодер v0x40 limits
payload проверяет 14 байт, VCO/input ranges и M/N/P. Refclk должен быть явно
получен из записи VBIOS либо идентифицированного crystal; автоматического
предположения 27 МГц нет. BIT C/PLL table и image-D translation подключены
ниже; v0x50 и legacy PLL formats здесь не реализованы.

Арифметика расчёта 64-битная с проверкой divisors/ranges. Возвращаются M/N/P,
encoded fractional N, расчётная частота и коэффициенты. План GF100 содержит
только CPU-данные трёх записей: control+0xc OR 0x100, coefficients+4,
fraction+0x10. Register base проверяется по head 0..3: разрешены банки
0x614100+head*0x800 и 0x614140+head*0x800. Второй вариант обнаружен в
VBIOS GTX 1050 Ti (86.07.39.80.0e): запись VPLL0 содержит 0x614140.
Control для плана читается именно из выбранного VBIOS банка +0x0c;
биты control другого банка не подставляются. Тест воспроизводит entry и
limits с фотографии аппаратного лога, перемещая вложенный указатель в
синтетический ROM. Это проверка разбора/расчёта, не аппаратного modeset.
Никакие записи или ожидание lock не выполняются; actual_khz — расчётное
значение, не аппаратное измерение частоты.

Эталонный тест использует синтетические ограничения: для 148500 кГц
M=2 N=219 P=20 fractional=4096, слова 0x0014db02/0x10000000.
Это не считанные limits GTX 1050 Ti. Проверяются неправильные размеры,
частоты, адреса регистров и 20000 мутаций limits. Исходники и лицензии
Linux v6.12 сохранены в upstream/pll с SHA256.

## BIT C и VPLL из VBIOS

`bios.c` находит BIT C v1 (pointer+8) / v2 (32-bit pointer+0), разбирает
PLL table v0x40 и записи типов 0x80..0x83. Header/entry lengths, полный span,
14-байтные вложенные limits и register base каждого head проверяются.
Все указатели используют существующую logical image-D translation и только
полностью захваченный ROM prefix. Повторный BIT C или VPLL type запрещён.
Результат публикуется только после успешного разбора всей PLL-таблицы.
Неизвестный формат/невалидная PLL-таблица не ломает иначе рабочие DCB/EDID,
но отключает PLL planning. BIT pointer errors по-прежнему общая BIT error.

Нулевой refclk разрешён в декодированных limits как предусмотренный NVKM
fallback на device crystal. Сам calculate по-прежнему требует ненулевой
refclk: caller обязан передать частоту из аппаратной идентификации карты,
а не константу. Host-тесты покрывают оба BIT C version, четыре VPLL,
zero refclk, malformed/duplicate records, вложенные image-D pointers и
ROM fuzz с BIT C в половине fixtures.

В native probe PLL report передаётся из BIOS через DDC к display planner.
Для выбранного head используется его VPLL entry и current control,
прочитанный через новый read-only BAR0 region 0x614000..0x615fff.
Если entry.refclk==0, применяется crystal_khz из уже выполненной аппаратной
идентификации GPU/strap; источник явно отмечается `crystal` вместо `VBIOS`.
При отсутствии частоты/entry, ошибочных limits или register base план не
строится. `VBIOS PLL` показывает статус/offset/version таблицы, `PLLPLAN`
показывает target/calculated kHz, refclk, M/N/P и fraction. Это CPU-план,
регистры PLL не записываются, частота на железе не измеряется.

Display host-тест проверяет crystal fallback, явный refclk, отсутствие refclk,
неверный BIOS register, последний head PLL control и cleanup всех десяти
read-only regions. Модель BAR целиком остаётся неизменной. Новые строки
`VBIOS PLL` и `PLLPLAN` требуют проверки с настоящим VBIOS GTX 1050 Ti.

На реальной карте PLL table найден по 0x04ff8, version 0x40, но ранний
декодер сообщил invalid. При сверке с NVKM исправлено лишнее ограничение:
нулевые min M/N/P допускаются как границы BIOS ranges. Ненулевыми должны
быть выбранные M/P (делители), а не обязательно минимальные границы.
N=0 также допускается для дробного коэффициента, как в gt215_pll_calc.
Добавлены регрессии zero minima, fractional N=0 и отсутствие usable M.
Причина invalid на самой карте пока не установлена без детальной записи;
исправление zero minima само по себе не считается аппаратно подтверждённым.

При PLL invalid журнал теперь показывает reason, logical record offset,
4-байтный table header, 11-байтную VPLL entry и 14 bytes limits (только из
проверенных spans). Различаются BIT payload, duplicate C/type, table header/
span, nested limits pointer, register base и limits ranges. Непроверенные
байты не читаются; при отсутствии данных соответствующие bytes нулевые.
Host-тест сверяет причины для каждого класса ошибки и содержимое проблемных
limits. Следующий аппаратный результат должен показать PLLPLAN либо эти
строки, чтобы не подменять реальную причину предположением.

## Аппаратный handoff перед загрузкой instance

На GTX 1050 Ti подтверждены `VBIOS PLL status=parsed` и расчёт для
148500 кГц: M=2 N=109 P=10 fraction=0x1000, refclk 27000 из VBIOS.
Это расчёт, PLL не программировался.

После успешного timing/PLL plan `engine_pci.c` выполняет короткий нативный
цикл take/capabilities/restore. Все read-only leases снимка освобождаются
до получения leases handoff. Проверяются 17 неактивных каналов; активный
канал не останавливается. Выполняются firmware request/ack, маскирование
прерываний с readback, копирование head/SOR capabilities в EVO mirrors,
затем восстановление mirrors, масок и firmware request с readback.
W1C handoff acknowledge не является восстанавливаемым состоянием.
Request/ack bits должны быть сброшены после handoff; отсутствие interrupt
handoff при активном request запрещает захват даже при ack=0.

Записи разрешены только в точные masks/handoff/capability регистры;
instance и channel controls, кроме core control, отображаются read-only.
Core control имеет отдельный DWORD lease для описанного ниже drain.
PRAMIN/VRAM/PLL вообще
не отображаются этим адаптером. Таймер проверяется до MMIO; ожидания
ограничены временем и числом итераций. Отказы до изменения состояния
освобождают leases; после изменения выполняется rollback. Невозможность
подтвердить восстановление останавливает ядро с сохранёнными mappings.

Журнал: `HANDOFF take=...`, `HANDOFF capabilities=...`, `HANDOFF restored`.
При BUSY дополнительно печатаются точные reg/value/mask/pending из чтения,
которое отклонило захват, и control index для channel blocker. Значения
сохраняются в engine до rollback, повторный MMIO snapshot их не подменяет.
На GTX 1050 Ti первый аппаратный цикл отклонён с `channels/firmware busy`
до создания snapshot/изменения регистров; конкретный blocker этим ранним
журналом не установлен. Проверку idle нельзя ослаблять без его определения.
Busy означает отсутствие захвата. Этот цикл ещё не разрешает загрузку
произвольной VRAM: нужны отдельный контракт владения диапазоном и исключение
firmware/scanout областей. Нативные upload/bind/channel start остаются
отключены до аппаратной проверки handoff. Host-тест адаптера проверяет
реальный take/capabilities/restore над моделью PCI mappings, все отказы
map/address, 17 busy channels, неподвижный таймер и rollback при неверной
capability. Сам аппаратный handoff этим тестом не подтверждён.

На GTX 1050 Ti диагностика выявила blocker core0: 0x610490=0x48070088,
pending=0x00060000. Allocation/connect/PUT requests (bits 0,1,4) и init busy
(bit31) выключены, но activity[20:17] ненулевая. Проверка idle сохраняется.
Добавлен ограниченный GF119 core fini только для этого класса состояний:
до записи проверяются выключенные requests core и idle остальных 16
каналов; затем requests повторно проверяются и записываются с очисткой
bit4 и bits0..1, как в upstream gf119_disp_core_fini. Уже включённый канал
никогда не останавливается этим путём. Descriptors, PUT, timing, instance
не изменяются; status fields не восстанавливаются сырым snapshot.
Ожидание сброса activity ограничено 2 секундами и 20001 чтением.

После успеха `HANDOFF disabled core drain=ok` повторяется обычный take,
который заново проверяет все каналы и firmware. При timeout/IO/busy
захват не продолжается. Повторная запись выключенных requests ещё не
подтверждена на железе: возможно, потребуется полный firmware teardown,
но ослабление маски activity не считается корректной заменой.
Host-регрессия использует точное значение с фотографии, покрывает оба
write failures, все read failures, busy других каналов, enabled core,
stalled/backward clock и неизменность VRAM/instance/PUT. Модель нативного
адаптера дополнительно проверяет bounded timeout при stuck status.

Аппаратный повторный stop до handoff дал timeout. Этот порядок больше не
используется: drain переносится после подтверждённого firmware request/ack.
Исключение preflight разрешает только disabled core0 с ненулевой activity,
при supervisor handoff bit 0x100 и неактивных остальных 16 каналах. Core
allocation/connect/PUT/init-busy по-прежнему запрещены. Без handoff bit
никаких drain-записей не выполняется; выводятся firmware/supervisor для
определения следующего шага. После handoff/drain ещё раз проверяется полный
idle mask всех каналов, затем маскируются IRQ и разрешаются capabilities.

При неудаче до изменения capabilities/IRQs/instance rollback возвращает
только firmware request с readback и не требует idle firmware-канала:
драйверных ресурсов в этом состоянии нет. После изменения ресурсов остаётся
прежнее обязательное idle перед rollback. Проверки моделируют firmware,
который разрешает drain только после request/ack, stuck core после ack,
все read/write отказы этого нового пути и восстановление request при timeout.
В журнал добавлены saved firmware/supervisor, `core drain after firmware`
и `core after drain`. Работоспособность нового порядка на GP107 ещё не
подтверждена; успешные host-модели не означают аппаратного handoff.

## Нативная проверочная загрузка instance в VRAM

На GTX 1050 Ti аппаратно подтверждены `core drain after firmware=ok`,
core control 0x48010088, `take=ok`, `capabilities=ok`, `restored`.
Следующий нативный шаг (`upload_pci.c`) выполняется между capabilities и
возвратом firmware: backup 64KiB, upload подготовленного/relocated instance
с RAMHT и DMA objects, readback всех DWORD, restore исходных DWORD и
повторный readback. 0x610010 не записывается, PUT/descriptors/channels/PLL
не изменяются; notifier и RAMHT остаются непривязанными.

Первый поддержанный профиль ограничен head0, одним firmware scanout с
выборочным соответствием VRAM=0, boot span <=16MiB, старым instance=1,
BAR1 context=0 и BAR3 context=0x40000000. Дополнительный head timing или
supervisor mask отключает upload. Первые 64MiB и последние 16MiB VRAM
исключены из локального pool; 64KiB scratch размещается после первых
64MiB. Это временная диагностическая область с обязательным восстановлением,
а не постоянный VRAM allocator или доказательство перевода boot BAR.
Доступ сериализован текущим PCI claim; другие GPU-драйверы/каналы не должны
использовать этот GPU. До backup, upload и restore подтверждаются quiescent
engine, все 17 idle channels, сброшенные firmware request/ack, неизменные
instance и физические BAR contexts. Сам scanout продолжает работать из
исключённой начальной области. Sampling не превращается в контракт владения
всей framebuffer памятью; hardware upload остаётся экспериментальным.

PRAMIN 0x1700 и aperture отображаются отдельными writable leases только
на время теста, после успешного handoff. Callback ограничивает адреса
ровно выделенными 64KiB; window select имеет readback. Исходный window
восстанавливается и проверяется перед unmap/возвратом firmware. При сбое
backup загрузка не начинается; при сбое upload применяется существующий
проверяемый rollback. Ошибка VRAM/window rollback останавливает ядро с
сохранённым состоянием/leases, не отдавая firmware изменённую память.

Ожидаемые строки: `UPLOAD instance/RAMHT verified bytes=65536 (not bound)`,
`UPLOAD original VRAM restored and verified`, `UPLOAD PRAMIN window restored`,
затем `HANDOFF restored; no instance bind/channel start`. Host-тест исполняет
нативный адаптер над моделью PCI/MMIO: проверяет загруженные RAMHT/notifier,
восстановление всех исходных регистров/VRAM/window, все новые map/address
и heap отказы, неподдержанный scanout и изменение BAR context после snapshot.
Транспортные отказы каждого слова и lost writes покрываются существующим
`nouveau_upload` тестом. Аппаратные upload/readback/restore ещё не подтверждены.

## Временная привязка instance

На GTX 1050 Ti подтверждены аппаратные `UPLOAD instance/RAMHT verified`,
`original VRAM restored and verified`, `PRAMIN window restored` и возврат
handoff. Следующий probe расширяет этот цикл временной привязкой instance.
После upload/readback выполняется `nouveau_engine_bind`: проверка idle,
pin upload, запись 0x610010 и точный readback. Native transport теперь
выделяет writable lease ровно на этот DWORD и разрешает запись только
при QUARANTINED engine с PINNED upload, только нового или сохранённого
instance значения. Descriptor/PUT/channel start/PLL пути не включены.

`nouveau_engine_unbind` отделён от полного engine restore: проверить idle,
вернуть старую instance/readback, unpin, restore/readback исходной VRAM.
Firmware, IRQ и capabilities при этом ещё удерживаются драйвером.
Это позволяет восстановить окно PRAMIN и освободить upload storage до
передачи управления firmware. Затем выполняется полный engine restore.
При неудаче bind запись могла попасть в железо: upload остаётся pinned,
требуется тот же unbind, а не преждевременный free. Ошибка unbind/VRAM
restore останавливает native probe с ресурсами и firmware под удержанием.

Ожидаемые строки: `BIND instance readback verified (channels stopped)` и
`BIND original instance restored and verified`, затем прежние UPLOAD restore
и HANDOFF restored. Host-модель проверяет instance=0x00040009 в момент bind,
старый instance=1 и VRAM после unbind, отсутствие активных каналов и утечек.
Отдельные тесты unbind покрывают каждый отказ register read/write, карантин,
запрет finish до восстановления и успешный повтор. Эта привязка пока не
проверена на карте; channels/UPDATE и изменение режима остаются отключены.

## Пустой core-канал

На GTX 1050 Ti аппаратно подтверждены временные bind/readback/unbind,
восстановление original instance/VRAM/PRAMIN и возврат firmware.
Следующий probe (`channel_pci.c`) между bind и unbind выделяет реальный
HOST DMA push 4KiB, обнуляет его и передаёт GPU через dma_device_acquire.
`prepared_bytes=0` теперь разрешён channel API для пустого start/stop;
любой kick, включая PUT=0, в таком состоянии отклоняется. Native adapter
вообще не вызывает kick и разрешает запись нового PUT только со значением 0.

Перед acquire проверяются bound/PINNED instance, firmware request/ack=0
и idle всех 17 каналов. Инициализация выполняет существующую GP102 sequence
с Pascal descriptor 0x611494/498/49c, HOST=3, limit=0x10000, target=1,
PUT=0 и core control=0x01000013. Готовность проверяется по снятию busy bit31,
readback descriptor и нулевому PUT; никаких timing/surface/UPDATE методов
в push-буфере нет. Начальный request bits snapshot не заменяет аппаратный
статус; управляющие request биты могут отличаться от статуса активности.

После старта (либо частично выполненного старта) выполняется GF119 stop с
ограниченным ожиданием. Затем восстанавливаются saved descriptor/PUT с
readback и известные control requests по mask 0x01000013. Status поля не
восстанавливаются сырым снимком регистра. Только после подтверждённой
остановки/восстановления выполняются dma_device_complete и dma_free.
Instance всё это время остаётся pinned; прежний unbind/VRAM rollback и
возврат firmware выполняются лишь после завершения channel probe.

Новые writable leases ограничены descriptor span 12 bytes и PUT DWORD;
вместе с handoff и PRAMIN используются 19 из 32 registry slots после
объединения соседних bank mappings (см. ниже). Ошибка
stop/restore удерживает DMA, upload, instance и mappings до halt/reset.
Недоступный DMA или ошибка allocation/map не запускает канал и оставляет
возможность обычного instance rollback. В журнале `CORECHANNEL start status`
показывает enum (0=OK), control и PUT; затем ожидаются `running empty PUT=0`
и `stopped; descriptors/PUT restored; DMA returned`, перед BIND/UPLOAD restore.

Host-проверка моделирует нативный полный upload/bind/start/stop/unbind цикл,
передачу/возврат DMA ownership, старый ненулевой PUT и descriptors, все новые
map/address отказы, недоступный/невыделенный DMA и отказ acquire. Отдельный
child тест с stuck activity подтверждает halt с device-owned DMA, pinned
instance и неизменённым порядком rollback. Channel unit-тесты продолжают
покрывать каждый read/write failure и timeout, а также запрет kick пустого
буфера. Сам аппаратный start/stop пока не подтверждён, modeset не выполняется.

Первая аппаратная попытка была отклонена до выделения push/запуска канала
общей проверкой `bound instance/HOST DMA unavailable`; прежние instance,
VRAM и PRAMIN восстановлены. Эта строка не доказывает недоступность DMA:
в неё также попадали engine tracking, pin и аппаратные firmware/instance
проверки. Общая причина заменена на `CORECHANNEL blocked reason=...` с
reg/observed/expected из отвергающего чтения (без повторного чтения,
подменяющего данные). Отдельно выводится HOST DMA identity unavailable.
Тест покрывает все классы допуска, точные firmware/instance mismatch
значения и отсутствие alloc/map/write на отклонённом пути. До аппаратного
результата точная причина отказа core-канала оставалась неизвестной.

Следующий аппаратный журнал дошёл до `CORECHANNEL PUT MMIO unavailable`.
В коде обнаружено исчерпание глобального registry: прежние 26 handoff
leases + 2 PRAMIN + 2 channel = 30, а BOOT/strap mappings в `pci.c` и DDC
lease в `ddc_pci.c` удерживаются до возврата вложенного display probe.
Таким образом последний PUT запрашивал минимум 33-й слот при лимите 32,
даже без прочих драйверов. Изолированный host-тест прежде не моделировал
внешние активные leases и не мог поймать этот случай.

Handoff mappings сокращены с 26 до 15: BAR contexts одним readonly span,
IRQ masks/ack одним span и восемь SOR capability mirrors одним bank span.
Exact write allow-list в transport не расширен: промежутки внутри span
не доступны для записи через callbacks. Instance/core leases остаются
отдельными DWORD, channel descriptors и PUT отображаются отдельно.
Полный вложенный цикл теперь требует 19 слотов (22 с вызывающими BOOT/
strap/DDC). Глобальный лимит и чужие leases не изменяются.

Host-regression моделирует занятые другими пользователями слоты: при 12
внешних leases start/stop/rollback проходит в общем лимите 32; при 14
внешних leases воспроизводится поздний отказ PUT с полным освобождением
собственных ресурсов и сохранением внешних. Все map/address fault indices
обновлены под компактную таблицу. Аппаратный start/stop после исправления
бюджета ещё требуется подтвердить.
