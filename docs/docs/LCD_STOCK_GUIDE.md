# Geiger-2: керування LCD, відновлене з оригінальної EN 4.1.17

Дата: 2026-10-17. MCU: STM32L051C8T6. Видима область: 160×80 px.

## 1. Головний висновок для поточного запуску

**LCD підключений до SPI2, а не SPI1.**

| Сигнал LCD | GPIO STM32 | Режим | Активний стан / значення |
|---|---|---|---|
| RESET / RESX | **PA8** | Output push-pull | 0 = reset, 1 = робота |
| CS / CSX | **PA11** | Output push-pull | **0 = вибрано LCD**, 1 = не вибрано |
| D/C / A0 | **PB11** | Output push-pull | **0 = команда, 1 = параметри або пікселі** |
| SCK / SCL | **PB13** | **AF0, SPI2_SCK** | SPI mode 0, idle low |
| MOSI / SDA | **PB15** | **AF0, SPI2_MOSI** | 8 біт, MSB first |
| MISO | Не використовується драйвером LCD | PB14 не налаштований як SPI2_MISO | Для заливки й команд читання не потрібне |

Критичні параметри:

- SPI2 = **0x40003800**.
- Шина: **8-бітний SPI mode 0**, CPOL=0, CPHA=0, MSB first.
- NSS — software. Зовнішній CS керується **PA11**; апаратний NSS PB12 для цього не потрібний.
- Формат пікселя: **RGB565, два байти на піксель, старший байт першим**.
- MADCTL: **0xA8**.
- Для поточного stock-орієнтування: **X offset = 0, Y offset = 24**.
- Повна заливка: X=0…159; Y у пам’яті контролера = 24…103.
- На дроті для повного кадру потрібно **25 600 байтів пікселів** після команди RAMWR.

Якщо поточний кастом використовує SPI1, PA5/PA7, CS=PA15, RESET=PB8 або плутає PA11 із D/C, треба виправити відповідність згідно з цією таблицею.

## 2. Що саме перевірено

Вхідний файл: **Port-4.1.17.hex**, побайтно однаковий із наданим раніше **Port-4.1.17(3).hex**.

SHA-256 HEX-файла:

~~~text
89368e9b3d8789d641d0d9756971cea92ea10902ea8feffac604745a0791d6d0
~~~

Перевірки:

1. Intel HEX декодовано з перевіркою контрольних сум записів.
2. Дизасембльовано GPIO init, SPI init, LCD init, parser таблиць, установку вікна, передачу команд/даних і заливку.
3. Оригінальні Thumb-функції виконані в Unicorn: GPIO init та SPI2 init виконувалися своїм машинним кодом; HAL_Delay і передача SPI були перехоплені для запису трасування.
4. Отримано SPI2 CR1=0x0304, CR2=0 та GPIO-конфігурацію PB13/PB15 AF0.
5. Оригінальна заливка віддала точний заголовок вікна й 12 800 двобайтових пікселів.
6. C-приклад нижче скомпільовано з host-harness; його потік D/C + байти та затримки збігаються з трасуванням оригіналу.

Це перевірка коду та потоку даних. На фізичній платі новий приклад у цій сесії не запускався. Поточний source кастомної прошивки не був наданий, тому конкретний помилковий рядок у ньому ще не встановлений.

Контролер використовує **командну систему ST7735-сумісного сімейства**. За HEX не встановлено фізичний ID і точну модифікацію кристала ST7735R/ST7735S. Для відтворення запуску нижче використані саме байти оригіналу, а не типова таблиця іншого LCD-модуля.

## 3. Докази за адресами оригінального коду

| Адреса STM32 | Відновлена роль | Що видно в коді |
|---|---|---|
| 0x08002DBC | Загальна ініціалізація GPIO плати | PA8, PA11, PB11 — push-pull output |
| 0x08002F2C | Ініціалізація SPI дисплея | handle 0x200002FC, Instance=0x40003800 |
| 0x080046FC | HAL_SPI_MspInit | Гілка SPI2 вмикає APB1 SPI2 clock та PB13/PB15 AF0 |
| 0x0800474E…0x0800477E | Гілка MSP саме для SPI2 | Pin mask=0xA000, Mode=2, Speed=3, Alternate=0 |
| 0x08005896 | HAL_GPIO_WritePin | state=1 пише BSRR, state=0 пише BRR |
| 0x08006BA0 | HAL_SPI_Init | Формує CR1/CR2 з handle |
| 0x08006C6C | HAL_SPI_Transmit | Передача команд і даних |
| 0x08008BEC | LCD write data | PB11=1, передає buffer через handle 0x200002FC |
| 0x08008C18 | LCD select | PA11=0 |
| 0x08008C2A | LCD unselect | PA11=1 |
| 0x08008C3C | LCD hardware reset | PA8=0, delay 5 ms, PA8=1 |
| 0x08008C64 | LCD write command | PB11=0, передає рівно 1 байт через SPI2 |
| 0x08008C94 | Parser командних таблиць | Довжина data в молодших 7 бітах, bit7 = наявність delay |
| 0x08008CE2 | LCD address window | Команди 0x2A, 0x2B; до Y додається 0x18; потім 0x2C |
| 0x08008D34 | LCD init | CS low → reset → три таблиці → CS high |
| 0x08008ECC | Fill rectangle | Установлює вікно, пише RGB565 |
| 0x08008F70 | Fill screen | Викликає rectangle(0,0,160,80,color) |
| 0x08008FD2 | Inversion on/off | 0 → команда 0x20, ненульове → 0x21 |

### Не плутати з зовнішньою NOR

У цьому HEX другий SPI-handle за 0x20000354 має Instance=SPI1, 0x40013000. Його MSP-гілка налаштовує **PB3/PB4/PB5 AF0**, а читання NOR використовує CS на PA15. LCD write-command/write-data звертаються до іншого handle, **SPI2 за 0x200002FC**.

Для суцільної заливки зовнішня SPI NOR, текстури, шрифти та resource dispatcher не потрібні. Ланцюжок заливки веде безпосередньо в SPI2.

## 4. SPI: параметри зі стоку та контроль регістрів

У 0x08002F2C в handle записано:

| Поле HAL | Значення з HEX | Значення за HAL STM32L0 |
|---|---:|---|
| Instance | 0x40003800 | SPI2 |
| Mode | 0x00000104 | Master, MSTR + SSI |
| Direction | 0 | 2 lines |
| DataSize | 0 | **8 bit** |
| CLKPolarity | 0 | Low |
| CLKPhase | 0 | First edge |
| NSS | 0x00000200 | Software |
| BaudRatePrescaler | 0 | PCLK1 / 2 |
| FirstBit | 0 | MSB |
| TIMode | 0 | Disabled |
| CRCCalculation | 0 | Disabled |
| CRCPolynomial | 7 | 7, CRC не увімкнений |

Stock SystemClock_Config за 0x08003378 задає HSI16 → PLL ×4 /2 = 32 MHz; AHB і APB1 — без ділення. Отже штатний SCK — близько **16 MHz**. Це виведено з конфігурації, не вимір осцилографом.

Для першого тесту в прикладі використано /32: приблизно **1 MHz при PCLK1=32 MHz**. Це діагностична зміна швидкості; решта режиму відповідає стоку.

### Що подивитися у debugger

| Регістр / поле | Очікування |
|---|---|
| RCC_IOPENR, 0x4002102C | GPIOAEN bit0=1, GPIOBEN bit1=1 |
| RCC_APB1ENR, 0x40021038 | SPI2EN bit14=1 |
| GPIOA.MODER для PA8 і PA11 | 01, output |
| GPIOB.MODER для PB11 | 01, output |
| GPIOB.MODER для PB13/PB15 | 10, alternate function |
| GPIOB.AFRH для PB13/PB15 | AF0, відповідні 4-бітні поля = 0 |
| GPIOB.OTYPER для PB11/PB13/PB15 | 0, push-pull |
| PUPDR для цих LCD-сигналів | 00, no pull |
| SPI2.CR1, 0x40003800, штатний /2 | 0x0304 до SPE; 0x0344 з SPE |
| SPI2.CR1, діагностичний /32 | 0x0324 до SPE; 0x0364 з SPE |
| SPI2.CR2, 0x40003804 | 0 |
| SPI2.CR1 DFF bit11 | 0, **SPI лишається 8-бітним при RGB565** |
| SPI2.SR, 0x40003808 | TXE bit1; BSY bit7; MODF bit5; OVR bit6 |

HAL_SPI_Transmit в оригіналі пише байти у DR, чекає завершення передачі та очищає OVR для 2-line mode читанням DR, потім SR. У власному polling-драйвері не міняти D/C або CS лише на підставі TXE: перед зміною дочекатись **BSY=0**. За software NSS мають бути встановлені і SSM, і SSI; інакше можливий MODF/скидання master.

## 5. Оригінальна послідовність старту LCD

У main:

1. GPIO init викликається з 0x08003840.
2. SPI2 init викликається з 0x08003848.
3. Перед LCD init: PB7=1 у 0x0800387A, delay 100 ms.
4. LCD_Init викликається з 0x08003884.
5. Inversion(false) викликається з 0x0800388A: окрема команда **0x20**.
6. Перша заливка викликається з 0x08003890: колір **0x1925**.

У самому LCD_Init:

- PA11=0: CS вибрано.
- PA8=0 → delay 5 ms → PA8=1.
- Таблиця 0x0800FAD0, 15 команд.
- Таблиця 0x0800FAC0, 3 команди.
- Таблиця 0x0800FA94, 4 команди.
- PA11=1: CS знято.

### Усі команди в точному порядку

Delay вказано після команди та її параметрів. Прочерк означає відсутність спеціальної затримки.

| № | Адреса запису в HEX | Команда | Data, hex | Delay, ms |
|---:|---|---|---|---:|
| 1 | 0x0800FAD1 | 01, SWRESET | — | 150 |
| 2 | 0x0800FAD4 | 11, SLPOUT | — | 500 |
| 3 | 0x0800FAD7 | B1 | 01 2C 2D | — |
| 4 | 0x0800FADC | B2 | 01 2C 2D | — |
| 5 | 0x0800FAE1 | B3 | 01 2C 2D 01 2C 2D | — |
| 6 | 0x0800FAE9 | B4 | 07 | — |
| 7 | 0x0800FAEC | C0 | A2 02 84 | — |
| 8 | 0x0800FAF1 | C1 | C5 | — |
| 9 | 0x0800FAF4 | C2 | 0A 00 | — |
| 10 | 0x0800FAF8 | C3 | 8A 2A | — |
| 11 | 0x0800FAFC | C4 | 8A EE | — |
| 12 | 0x0800FB00 | C5 | 0E | — |
| 13 | 0x0800FB03 | 20, INVOFF | — | — |
| 14 | 0x0800FB05 | 36, MADCTL | **A8** | — |
| 15 | 0x0800FB08 | 3A, COLMOD | **05** | — |
| 16 | 0x0800FAC1 | 2A, CASET | 00 00 00 4F | — |
| 17 | 0x0800FAC7 | 2B, RASET | 00 00 00 9F | — |
| 18 | 0x0800FACD | 21, INVON | — | — |
| 19 | 0x0800FA95 | E0 | 02 1C 07 12 37 32 29 2D 29 25 2B 39 00 01 03 10 | — |
| 20 | 0x0800FAA7 | E1 | 03 1D 07 06 2E 2C 29 2D 2E 2E 37 3F 00 00 02 10 | — |
| 21 | 0x0800FAB9 | 13, NORON | — | 10 |
| 22 | 0x0800FABC | 29, DISPON | — | 100 |

Після повернення з LCD_Init main знову надсилає **20, INVOFF**. Таким чином остаточний stock-стан інверсії відрізняється від останнього INVON у другій таблиці.

Ініціалізація передає **22 командні байти та 66 байтів параметрів**, разом 88 байтів. Затримки: 5 ms hardware reset, потім 150/500/10/100 ms. HAL_Delay може додавати один системний tick.

Якщо копіювати сирі таблиці, bit7 байта довжини — ознака delay, а delay-byte FF означає **500 ms**. FF не є тут пікселем або параметром команди. У C-прикладі таблиці вже розкодовані.

## 6. Адресація: саме 160×80 з Y+24

Функція 0x08008CE2 приймає x0,y0,x1,y1 і передає:

~~~text
D/C=0: 2A
D/C=1: 00 x0 00 x1
D/C=0: 2B
D/C=1: 00 (y0+24) 00 (y1+24)
D/C=0: 2C
D/C=1: pixel bytes...
~~~

Для повного екрана:

~~~text
CS=0
D/C=0  2A
D/C=1  00 00 00 9F
D/C=0  2B
D/C=1  00 18 00 67
D/C=0  2C
D/C=1  [pixel_hi pixel_lo] × 12800
CS=1
~~~

MADCTL=0xA8 задає MY + MV + BGR. COLMOD=0x05 задає 16-бітний піксель; це не переводить SPI на 16-бітні кадри.

Початкові CASET/RASET у таблиці виглядають як 80×160, але **кожне малювання перевизначає вікно** через 0x08008CE2. Для stock-ландшафтного режиму не використовувати лише ті початкові 80×160 межі.

Пікселі заливки передаються старшим байтом першими. Наприклад, значення 0xF800 → F8 00. Пряма передача little-endian uint16_t-буфера без перестановки байтів дасть неправильні кольори. У stock-програмі байти растрових SPI-ресурсів є окремим випадком; не переносити їхній файловий порядок на новий framebuffer автоматично.

## 7. Мінімальний C-приклад для STM32L0 HAL

Передумови: HAL_Init, системне тактування й працездатний мілісекундний HAL_Delay; живлення цифрової частини LCD увімкнене. Поточний спосіб керування підсвіткою використати у своїй board-init.

Приклад конфігурує лише п’ять підтверджених сигналів LCD. Це не повна ініціалізація плати дозиметра. HAL_SPI_MspInit у проєкті має узгоджуватися з цією SPI2-конфігурацією.

Передачі блокувальні; кожен результат HAL перевіряється. Замість повного framebuffer на 25 600 B використано 320-байтовий буфер рядка: MCU має лише 8 KiB SRAM.

Відмінності прикладу від реалізації стоку: діагностичний prescaler /32 замість /2, таймаут HAL 1000 ms замість HAL_MAX_DELAY та передача пікселів по рядках замість окремих двобайтових викликів. Командні байти, параметри, порядок піксельних байтів і номінальні init-затримки збережені.

~~~c
#include "stm32l0xx_hal.h"
#include <stdbool.h>
#include <stddef.h>

static SPI_HandleTypeDef lcd_spi;

static void lcd_cs(bool high) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void lcd_dc(bool data) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, data ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static bool lcd_tx(const uint8_t *p, uint16_t count) {
    return HAL_SPI_Transmit(&lcd_spi, (uint8_t *)p, count, 1000U) == HAL_OK;
}
static bool lcd_command(uint8_t cmd) {
    lcd_dc(false);
    return lcd_tx(&cmd, 1U);
}
static bool lcd_data(const uint8_t *p, uint16_t count) {
    lcd_dc(true);
    return lcd_tx(p, count);
}

bool geiger_lcd_io_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8 | GPIO_PIN_11, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET);

    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8 | GPIO_PIN_11;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOB, &g);

    g.Pin = GPIO_PIN_13 | GPIO_PIN_15;
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF0_SPI2;
    HAL_GPIO_Init(GPIOB, &g);

    lcd_spi.Instance = SPI2;
    lcd_spi.Init.Mode = SPI_MODE_MASTER;
    lcd_spi.Init.Direction = SPI_DIRECTION_2LINES;
    lcd_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    lcd_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    lcd_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    lcd_spi.Init.NSS = SPI_NSS_SOFT;
    /* Diagnostic speed: PCLK1/32. Stock uses SPI_BAUDRATEPRESCALER_2. */
    lcd_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    lcd_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    lcd_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    lcd_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    lcd_spi.Init.CRCPolynomial = 7;
    return HAL_SPI_Init(&lcd_spi) == HAL_OK;
}

typedef struct {
    uint8_t cmd, len;
    uint16_t delay_ms;
    uint8_t data[16];
} LcdInitStep;

static const LcdInitStep lcd_init_steps[] = {
    {0x01, 0, 150, {0}},
    {0x11, 0, 500, {0}},
    {0xB1, 3,   0, {0x01,0x2C,0x2D}},
    {0xB2, 3,   0, {0x01,0x2C,0x2D}},
    {0xB3, 6,   0, {0x01,0x2C,0x2D,0x01,0x2C,0x2D}},
    {0xB4, 1,   0, {0x07}},
    {0xC0, 3,   0, {0xA2,0x02,0x84}},
    {0xC1, 1,   0, {0xC5}},
    {0xC2, 2,   0, {0x0A,0x00}},
    {0xC3, 2,   0, {0x8A,0x2A}},
    {0xC4, 2,   0, {0x8A,0xEE}},
    {0xC5, 1,   0, {0x0E}},
    {0x20, 0,   0, {0}},
    {0x36, 1,   0, {0xA8}},
    {0x3A, 1,   0, {0x05}},
    {0x2A, 4,   0, {0x00,0x00,0x00,0x4F}},
    {0x2B, 4,   0, {0x00,0x00,0x00,0x9F}},
    {0x21, 0,   0, {0}},
    {0xE0,16,   0, {0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,
                   0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10}},
    {0xE1,16,   0, {0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,
                   0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10}},
    {0x13, 0,  10, {0}},
    {0x29, 0, 100, {0}}
};

bool geiger_lcd_init(void) {
    lcd_cs(false);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);

    bool ok = true;
    for (size_t i = 0; i < sizeof lcd_init_steps / sizeof lcd_init_steps[0]; ++i) {
        const LcdInitStep *s = &lcd_init_steps[i];
        if (!lcd_command(s->cmd) || (s->len && !lcd_data(s->data, s->len))) {
            ok = false;
            break;
        }
        if (s->delay_ms) HAL_Delay(s->delay_ms);
    }
    lcd_cs(true);
    if (!ok) return false;

    /* Stock main() calls inversion-off after LCD_Init(). */
    lcd_cs(false);
    ok = lcd_command(0x20);
    lcd_cs(true);
    return ok;
}

bool geiger_lcd_fill(uint16_t color) {
    uint8_t row[160U * 2U];
    for (size_t i = 0; i < 160U; ++i) {
        row[2U*i]     = (uint8_t)(color >> 8);
        row[2U*i + 1] = (uint8_t)color;
    }
    const uint8_t cols[4] = {0x00,0x00,0x00,0x9F};
    const uint8_t rows[4] = {0x00,0x18,0x00,0x67};
    lcd_cs(false);
    bool ok = lcd_command(0x2A) && lcd_data(cols, sizeof cols)
           && lcd_command(0x2B) && lcd_data(rows, sizeof rows)
           && lcd_command(0x2C);
    for (unsigned y = 0; ok && y < 80U; ++y)
        ok = lcd_data(row, sizeof row);
    lcd_cs(true);
    return ok;
}
~~~

Порядок виклику після HAL/clock/board-init:

~~~c
if (!geiger_lcd_io_init()) {
    /* stage = 1: inspect clock, GPIO AF and HAL status */
    Error_Handler();
}
if (!geiger_lcd_init()) {
    /* stage = 2: inspect CS/DC/RESET and command transfers */
    Error_Handler();
}

static const uint16_t colors[] = {
    0xFFFF, 0xF800, 0x07E0, 0x001F, 0x0000
};
for (;;) {
    for (unsigned i = 0; i < sizeof colors / sizeof colors[0]; ++i) {
        if (!geiger_lcd_fill(colors[i])) Error_Handler();
        HAL_Delay(1000);
    }
}
~~~

Головна ознака успіху на першому тесті — екран помітно змінюється. Якщо червоний/синій переплутані, це окреме питання кольорового порядку; воно не пояснює повну відсутність змін. Спочатку залишити A8 та всю таблицю оригіналу.

## 8. Перевірка логічним аналізатором

Підключення: GND, **PB13**, **PB15**, **PA11**, **PB11**, **PA8**.

1. Для першого тесту зменшити SPI до близько 0.5–1 MHz, щоб зручно бачити фронти.
2. Декодер SPI: SCK=PB13, MOSI=PB15, CS=PA11 active-low, mode 0, MSB first, 8 bit.
3. Побачити PA8 low приблизно 5 ms, потім high.
4. На PA11=0 повинні з’явитися 01, пауза 150 ms, 11, пауза 500 ms.
5. На передачі команд PB11=0. На передачі параметрів і пікселів PB11=1.
6. Після таблиць повинні бути 29 і пауза 100 ms, потім окрема 20.
7. Перша заливка повинна починатись з **2A / 0000009F / 2B / 00180067 / 2C**.
8. Для кольору 0xF800 після 2C — **F8 00**, повторені 12 800 разів. Разом із командами/вікном це **25 611 байтів** однієї заливки.

Першу перевірку можна робити на GPIO MCU; якщо потік правильний там, а панель мовчить, наступний крок — перевірити ці ж сигнали біля шлейфа та живлення цифрової частини LCD.

## 9. Діагностика за симптомами

| Спостереження | Що перевіряти першим |
|---|---|
| Підсвітка світиться, PB13 не має імпульсів | Чи реально використовується SPI2; APB1 SPI2EN; PB13/PB15 AF0; чи код дійшов до передачі |
| Код завис перед першою командою | HAL_Delay та SysTick: чи зростає tick, чи не вимкнені IRQ |
| HAL_SPI_Transmit повертає BUSY/ERROR/TIMEOUT | Стан handle, правильний Instance, SSM/SSI, MODF, clock, перебіг SysTick |
| SPI є на інших пінах | Переплутана SPI-шина або альтернативна функція |
| PA11 завжди high | LCD не вибраний; замінити NSS/CS-логіку на ручний PA11 |
| PA8 лишається low | Контролер утримується в reset |
| Команди видно, PB11 завжди high | Команди йдуть як data; 01/11/29 не виконуються |
| PB11 завжди low | Параметри й пікселі інтерпретуються як команди |
| Лише команди, після 2C немає довгої передачі | Не виконується цикл заливки або неправильний розмір/повернення HAL |
| Зміни є тільки у вузькій смузі/частині екрана | MADCTL A8, Y+24, X=0…159, Y=24…103; кількість пікселів |
| Сміття/артефакти на межі команд | Чи D/C/CS не змінюється до BSY=0; перевірити неблокувальні DMA-виклики |
| Правильна повна траса, а екран зовсім не реагує | Цифрове VDD LCD, reset на шлейфі, цілісність з’єднань, узгодження зі штатними board-enable станами |

Власна заливка не вимагає текстур/шрифтів. Не шукати її повну відсутність у порожніх RGB565 ресурсах зовнішньої NOR.

## 10. Підсвітка, живлення та інші GPIO: межі доказу

Фізичну електричну схему транзисторів живлення лише з HEX остаточно встановити не можна. Тому такі виходи не позначені як підтверджені LCD_VDD/LCD_BL:

| GPIO | Точна поведінка в оригіналі | Що ще не доведено |
|---|---|---|
| **PB7** | Output PP; high у GPIO init (0x08002E3E) і перед LCD init (0x0800387A), потім delay 100 ms | Послідовність прив’язана до запуску LCD, але фізична роль (LCD board-enable чи інший спільний enable) не доведена. Повторна звірка інструкцій підтвердила саме PB7: перед викликом r1 заново завантажується значенням 0x80; попередня примітка про PB14 була помилковою |
| **PA14** | Output PP; high у GPIO init (0x08002E34), після першої заливки (0x0800389E) і на шляхах пробудження | Призначення не доведено; HEX не показує, що це HV enable. PA14 має функцію SWCLK: сліпе копіювання всього stock GPIO init може завадити налагодженню |
| **PB2** | Output PP; high на активних/пробуджувальних шляхах, low на sleep; є high після відмальовки startup-екрана (0x0800A59C) | Можливий BL/enable, але конкретний транзистор/навантаження за наявними фото не встановлено |

Якщо цифрові сигнали з розділу 8 правильні, перевірити, чи LCD має цифрове живлення, а не лише струм у LED підсвітки. Підсвітка та внутрішній контролер панелі — різні електричні ланцюги.

## 11. Sleep/wake у стоковому драйвері

- Sleep: CS low → команда **10, SLPIN** → delay **200 ms** → CS high. Приклади: 0x080036E2…0x080036F2 та 0x08004582…0x08004592.
- Wake без втрати живлення: CS low → команда **11, SLPOUT** → delay **200 ms** → CS high. Приклад: 0x080039DA…0x080039EA.
- Після втрати цифрового живлення або reset виконувати повну LCD init, а не лише SLPOUT.

## 12. Що зробити в поточному кастомі першочергово

1. Звірити п’ять GPIO з таблицею розділу 1.
2. Перевести LCD-передачі саме на SPI2 з PB13/PB15 AF0, 8 bit, mode 0, software NSS.
3. Звірити CS=PA11, D/C=PB11, RESET=PA8.
4. Повторити повну stock-таблицю, затримки та post-init INVOFF.
5. Виконати одну заливку з точним вікном 2A 0000009F, 2B 00180067.
6. Подивитись трасу на п’яти підтверджених сигналах. За цим уже можна локалізувати, чи проблема в конфігурації STM32, командному потоці, живленні або шлейфі.

## 13. Первинні джерела для значень регістрів і команд

Основне джерело pin map, init-таблиць і геометрії — наданий оригінальний HEX з SHA-256 вище.

Для тлумачення режимів використано:

- [STMicroelectronics DS10184: STM32L051x6/x8, Table 17](https://www.st.com/resource/en/datasheet/stm32l051t8.pdf) — PB13/PB15 AF0 SPI2.
- [STMicroelectronics: STM32L0 HAL SPI header](https://github.com/STMicroelectronics/stm32l0xx-hal-driver/blob/master/Inc/stm32l0xx_hal_spi.h) — SPI mode, NSS, data size, prescaler.
- [STMicroelectronics: STM32L051 CMSIS device header](https://github.com/STMicroelectronics/cmsis-device-l0/blob/master/Include/stm32l051xx.h) — адреси й біти регістрів.
- [STMicroelectronics: STM32L0 RCC header](https://github.com/STMicroelectronics/stm32l0xx-hal-driver/blob/master/Inc/stm32l0xx_hal_rcc.h) — PLL і bus dividers.
- [Sitronix ST7735S datasheet, vendor-authored PDF](https://cdn.sparkfun.com/assets/9/0/2/5/8/ST7735S_v1.1.pdf) — розділ 9.4, команди MADCTL/COLMOD/CASET/RASET/RAMWR.

Зовнішні datasheet-посилання пояснюють семантику. Вони не використовувалися для підміни stock-таблиці типовою конфігурацією іншого дисплея.


## 14. Зіставлення з кастомною прошивкою

Під час звірки гайда з `src/board_stm32l051.c` виявили такі відмінності від стокового запуску:

1. Після апаратного RESET кастом надсилав `0x0F 0x80` і чекав 150 ms. У стоку першою йде `SWRESET (0x01)`, також із затримкою 150 ms. `0x0F 0x80` у стоковому старті відсутня.
2. Після `SLPOUT (0x11)` кастом чекав 255 ms замість 500 ms із таблиці стоку.
3. Перед `LCD_Init` кастом не встановлював PB7=1 і не чекав 100 ms. Сток встановлює цей рівень перед ініціалізацією LCD; його фізична роль лишається невідомою, але стан відтворено.
4. Сток після завершення init відпускає CS і окремою транзакцією надсилає `INVOFF (0x20)`. Кастом залишав стан після `INVON (0x21)` з init-таблиці.

Ці відмінності виправлені в кастомній прошивці: PB7 high + 100 ms, апаратний reset, `0x01` + 150 ms, `0x11` + 500 ms, stock-таблиці, а потім окремий CS цикл для `0x20`. Збережені GPIO, SPI2, повна заливка й діагностична частота /32. Збірка й запис через COM4 успішні; користувач підтвердив, що дисплей показує кольори. Далі заливку прискорили через DMA; поточний результат зафіксований у [журналі дисплейного вузла](LCD_BRINGUP.md).
