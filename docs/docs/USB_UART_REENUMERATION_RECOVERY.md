# Ідея: програмне перепідключення USB–UART перед прошиванням

Дата: 2026-10-24  
Пристрій: Mira Safety Geiger‑2 / HiStand, STM32L051C8T6  
Статус: Web Serial open/close без UART-трафіку відтворює симптом; імовірна причина — Windows Chromium активує DTR і RTS під час open.

## Спостереження

- Фізичної кнопки входу в bootloader немає.
- Команда нижче нормально переводить прилад у STM32 ROM bootloader без перетикання USB:

```bat
port\STM32\STM32_Programmer_CLI -c port=COM3 br=115200 p=even rts=high dtr=high
```

- Якщо після цього одразу запустити прошивання через PlatformIO, на початку з’являється ще один reset у ROM, і прошивання не проходить очікуваним шляхом.
- Після фізичного від’єднання й повторного підключення USB перед прошиванням PlatformIO працює нормально.
- Причина поки невідома. Перетикання може скидати стан USB–UART моста, COM-драйвера/handle або змінювати RTS/DTR. Немає підтвердження, що для цього потрібне саме зняття живлення з MCU.

## Дотичний дефект: звичайне підключення застосунку на desktop

- Користувач підтвердив, що Android WebUSB підключається нормально; проблема стосується desktop-підключення через Web Serial.
- До зміни застосунок після `SerialPort.open()` ще явно викликав `setSignals(DTR=false, RTS=false)`. Це був додатковий перехід ліній поверх станів, які ОС/драйвер можуть виставити під час самого `open()`.
- Після прибирання цього виклику користувач спостерігає: при connect явного reset немає; при disconnect прилад перезапускається. Висмикування USB, поки порт відкритий, раніше заводило прилад у ROM bootloader; цей відомий сценарій повторно тестувати не потрібно.
- У firmware USART1 працює фіксовано на 115200 8N1. У звичайній гілці disconnect застосунок не надсилає reset-команду, а USART ISR/обробник протоколу не має дії на втрату COM-порту. Це зміщує підозру до `SerialPort.close()`, поведінки CP210x/Windows VCP або електричної реакції плати; причина не доведена.
- Read-only перевірка цього ПК визначила міст як Silicon Labs CP210x COM3, instance ID `USB\VID_10C4&PID_EA60\0001` (driver `oem51.inf`).
- Окремий тест `pnputil /restart-device` виконано вручну п'ять разів; після кожного запуску консоль перевіряли, збій не відтворився. PnP restart сам по собі наразі не виглядає причиною проблеми.

## Перевірка життєвого циклу порту в застосунку

Перегляд коду показав:

- Звичайне від'єднання застосунку не надсилає firmware-команду reset. Воно зупиняє polling, відхиляє незавершені запити, скасовує читання та закриває COM-порт.
- Було виявлено і виправлено в JS-клієнті: `SerialProtocol.disconnect()` тепер забороняє нові запити, чекає завершення активних записів і закриває writable stream перед `SerialPort.close()`. Раніше lock звільнявся без drain; при збігу Disconnect із незавершеним записом це могло обірвати UART-кадр. Така гонка могла вплинути на наступну команду, але не пояснює фізичний reset. Виправлення ще потребує апаратної перевірки.
- На firmware немає події «host закрив COM». Протокольний parser залишається активним між сесіями; кадр завершується нульовим байтом, а частковий/пошкоджений кадр відкидається на delimiter або при переповненні. Новий `HELLO` після disconnect не має спеціальної команди очищення parser state.
- `pnputil /restart-device` перевірено п'ять разів: після кожного запуску консоль працювала, збій не повторився. Перезапуск PnP не є відтворювачем проблеми.
- Безтрафіковий [`webserial_open_close_probe.html`](../../tools/webserial_open_close_probe.html) теж відтворив симптом: після `OPEN completed` прилад перестає подавати ознаки, після `CLOSE completed` ребутиться. Отже, UART-протокол і прикладні команди не потрібні для відтворення.
- У Chromium Windows backend `ConfigurePortImpl()` задає `fDtrControl = DTR_CONTROL_ENABLE` і, за `flowControl: 'none'`, `fRtsControl = RTS_CONTROL_ENABLE` під час налаштування відкритого порту. Це означає активні DTR та RTS при open. Web Serial API не має параметрів DTR/RTS у `SerialOptions`; `setSignals()` дозволено лише після відкриття, тож сторінка не може задати безпечні стани до цього переходу. Це узгоджується з probe та є головною гіпотезою фізичного входу в ROM/перезапуску, але ще не доводить електричне з'єднання цих ліній на платі.

Безтрафіковий probe відтворив симптом і у Firefox: після `OPEN completed` прилад перестає подавати ознаки, після `CLOSE completed` ребутиться. Отже, UART-протокол і прикладні команди не потрібні для відтворення; причина спільна для Web Serial на Windows або нижчого рівня. Chrome WebUSB із оновленою діагностикою завершився `WebUSB failed while opening the CP210x USB device (SecurityError): ... Access denied`. Відмова стається на `USBDevice.open()`, ще до `claimInterface()` та налаштування UART. Це вказує на блокування доступу до USB-пристрою драйвером/ОС, а не на помилку endpoint. У Firefox кнопки немає, бо браузер не надає `navigator.usb`.

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

PnP restart не відтворив дефект; безтрафіковий Web Serial probe відтворив його у браузерах на Windows. Chrome WebUSB не може виконати навіть `USBDevice.open()` з поточним CP210x VCP-драйвером. Для WebUSB на Windows потрібен WinUSB-драйвер; не замінювати ним CP210x VCP автоматично, бо це може прибрати COM-порт і зламати STM32 Programmer. Для Windows наступний практичний шлях — native helper/оболонка, яка відкриває COM із контрольованим DCB-профілем і передає байти застосунку через localhost/native messaging. Існуючий native probe уже показав, що відкриття з офіційним профілем (DTR asserted, RTS deasserted) не перезапускало прилад. На Android залишити наявний прямий WebUSB transport.

Уточнення (2026-10-28): повідомлення, що прилад не подає ознак, доки порт відкритий, стосувалося браузерного Web Serial у `app/index.html`, а не Windows EXE. У першому релізі після Web Serial `open()` явно виставлялися DTR=0/RTS=0; цей крок випадково зник під час пізніших змін і тепер повертається лише для Web Serial. Windows EXE використовує окремий native COM bridge і залишений на раніше перевіреному профілі.

Підстава: [Chrome for Developers — Windows WebUSB потребує WinUSB](https://developer.chrome.com/docs/capabilities/build-for-webusb).

Довідка: [Microsoft PnPUtil command syntax](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax).

Джерела щодо сигналів Web Serial: [Chromium Windows serial backend](https://chromium.googlesource.com/chromium/src/+/988ef7646e298b923c329e1d56da216912544494/services/device/serial/serial_io_handler_win.cc#232), [Web Serial spec](https://serial.spec.whatwg.org/#open-method) та [відкрите обговорення початкового стану DTR/RTS](https://github.com/whatwg/serial/issues/177).

## Результат ручного PnP restart (2026-10-28)

Користувач виконав у PowerShell від адміністратора:

```bat
pnputil /restart-device "USB\VID_10C4&PID_EA60\0001"
```

Користувач повторив команду п'ять разів і після кожного запуску перевірив консоль; збій не відтворився. Це свідчить, що PnP restart CP210x сам по собі не відтворює проблему. Натомість безтрафіковий Web Serial probe її відтворив.
