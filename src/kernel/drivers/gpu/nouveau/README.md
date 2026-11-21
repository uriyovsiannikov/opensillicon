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
