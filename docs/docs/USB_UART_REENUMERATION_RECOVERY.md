# Ідея: програмне перепідключення USB–UART перед прошиванням

Дата: 2026-10-24  
Пристрій: Mira Safety Geiger‑2 / HiStand, STM32L051C8T6  
Статус: гіпотеза для перевірки; ще не підтверджено тестом `pnputil`.

## Спостереження

- Фізичної кнопки входу в bootloader немає.
- Команда нижче нормально переводить прилад у STM32 ROM bootloader без перетикання USB:

```bat
port\STM32\STM32_Programmer_CLI -c port=COM3 br=115200 p=even rts=high dtr=high
```

- Якщо після цього одразу запустити прошивання через PlatformIO, на початку з’являється ще один reset у ROM, і прошивання не проходить очікуваним шляхом.
- Після фізичного від’єднання й повторного підключення USB перед прошиванням PlatformIO працює нормально.
- Причина поки невідома. Перетикання може скидати стан USB–UART моста, COM-драйвера/handle або змінювати RTS/DTR. Немає підтвердження, що для цього потрібне саме зняття живлення з MCU.

## Перевірювана гіпотеза

Замість фізичного перетикання перезапустити у Windows саме USB–UART пристрій через Plug and Play. Це змусить Windows від’єднати й заново виявити USB-пристрій/драйвер, але не гарантує вимкнення VBUS або живлення MCU.

Тестовий порядок:

1. Виконати наявну команду STM32 Programmer для входу в ROM bootloader.
2. Переконатися, що STM32 Programmer закрив COM-порт.
3. Виконати `pnputil /restart-device "<USB device instance ID>"` для USB–UART моста.
4. Дочекатися, поки COM-порт зникне й з’явиться знову.
5. Запустити прошивання через PlatformIO.
6. Зафіксувати, чи зник повторний reset/чи проходить запис.

Знайти точний instance ID можна командою:

```bat
pnputil /enum-devices /connected
```

Вибирати USB–UART пристрій за VID/PID/назвою та зв’язком із COM3; передавати сам `COM3` замість instance ID не можна. Точна модель моста ще не підтверджена.

## Запуск із JavaScript

Звичайна сторінка браузера не може запускати `pnputil.exe`. Потрібен локальний Windows-процес: наприклад, Node.js/Electron оболонка або невеликий native helper. Для Node.js виклик може виглядати так:

```js
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const execFileAsync = promisify(execFile);

async function restartUsbUart(instanceId) {
  // Спершу закрити COM-порт у serial-клієнті.
  const { stdout, stderr } = await execFileAsync(
    'pnputil.exe',
    ['/restart-device', instanceId],
    { windowsHide: true },
  );
  return { stdout, stderr };
}
```

Після виклику треба чекати реального повернення COM-порту, а тоді починати PlatformIO upload. На деяких системах для PnP restart потрібні права адміністратора. Команда `/restart-device` підтримується у Windows 10 версії 2004 і новіших.

## Важливі розрізнення

- Web Serial `close()` → `open()` закриває й повторно відкриває COM-сеанс, але не робить фізичного unplug і не гарантує перезапуск USB–UART моста.
- `pnputil /restart-device` — сильніший тест повторної ініціалізації PnP/USB пристрою, але це не гарантований USB VBUS power-cycle.
- Керований USB-хаб/реле з перемиканням живлення потрібен лише якщо тест покаже, що PnP restart не відтворює результат фізичного unplug.
- Не плутати цей крок із входом у bootloader: ROM уже запускається наявною послідовністю RTS/DTR. PnP restart перевіряється окремо після неї.
- Не вмикати автоматичний PnP restart після довільного timeout: спочатку підтвердити, що він не виводить MCU з ROM і що не шкодить даним/сеансу.

## Висновок для наступного кроку

Спершу перевірити вручну `pnputil /restart-device` між наявною командою входу в ROM і PlatformIO. Якщо це усуває проблему, автоматизувати ту саму послідовність локальним Windows helper-ом. Якщо ні — окремо дослідити RTS/DTR при повторному відкритті PlatformIO та поведінку фізичного USB unplug; не вважати живлення причиною без тесту.

Довідка: [Microsoft PnPUtil command syntax](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax).
