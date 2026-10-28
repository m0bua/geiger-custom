# Індекс довідкових матеріалів

Детальний розбір стокового ADC і живлення — [STOCK_BATTERY_ADC_AND_POWER](STOCK_BATTERY_ADC_AND_POWER.md). Компонентні документи: [LCD_STOCK_GUIDE](LCD_STOCK_GUIDE.md) описує LCD зі
стокового HEX, [LCD_AND_DISPLAY](LCD_AND_DISPLAY.md) — перевірену реалізацію
екрана, а [STOCK_RATE_DOSE](STOCK_RATE_DOSE.md) — алгоритм вимірювань у стоку.

## Вихідні матеріали

- reference/source-material/Port-4.1.17.hex — оригінальна англомовна стокова прошивка, тільки для читання/аналізу.
- reference/source-material/Milerd_HiStand_Dosimeter_Manual_19-01-23_WEB.pdf — інструкція приладу.
- reference/source-material/Geiger2-eeprom.bin — 2 KiB дамп, заповнений нулями.
- reference/board-photos/ — десять вихідних фото плати, що були надані для аналізу.
- reference/legacy-v9-export.zip — старий експорт експериментів і патчів до стокової прошивки.

## Звіти про прошивку та пам’ять

- [resource-call-audit.md](../reference/stock-analysis/resource-call-audit.md) — метод і межі індексу ресурсних викликів, перевірки SPI та EEPROM-читань.
- [asset-call-index.md](../reference/stock-analysis/asset-call-index.md) — відомі RGB565 ресурси й фізичні адреси у SPI NOR.
- [resource-call-index-216.csv](../reference/stock-analysis/resource-call-index-216.csv) — індекс 216 звернень до зовнішніх ресурсів.
- [all-FF-resource-ranges.csv](../reference/stock-analysis/all-FF-resource-ranges.csv) — блоки, що у перевіреному дампі повністю складаються з FF.
- [port-memory-map.md](../reference/stock-analysis/port-memory-map.md) — карта внутрішньої Flash і обмеження статичного reachability-аналізу.
- [GEIGER2_FONT_MAP.md](../reference/stock-analysis/GEIGER2_FONT_MAP.md) — карта шрифтів експериментальних/модифікованих збірок; не є повною картою стоку.
- [font-renderer-call-inventory-v9.md](../reference/stock-analysis/font-renderer-call-inventory-v9.md) — call-site інвентаризація певної версії шрифтового рендерера.

## Звіти про застосунок і протокол

- [APP_WEB_SERIAL_AND_CUSTOM_PROTOCOL.md](APP_WEB_SERIAL_AND_CUSTOM_PROTOCOL.md) — аналіз MilerdPort/стокового UART, кастомного протоколу та фізичних тестів Web Serial і native Windows на COM3.
- [ANDROID_PWA_USB_SPEC.md](ANDROID_PWA_USB_SPEC.md) — ТЗ на Android WebUSB-підключення через CP210x і встановлення вебзастосунку як PWA.
- [POWER_SAVING_AND_CUSTOM_RECOMMENDATIONS.md](POWER_SAVING_AND_CUSTOM_RECOMMENDATIONS.md) — вимоги й досліди з енергоспоживанням.
- [FLASHLIGHT_AND_SOUND_PORTING.md](FLASHLIGHT_AND_SOUND_PORTING.md) — статичний аналіз стокових функцій ліхтарика й звуку.

Попередній SPI BIN не знайдений у доступних матеріалах під час складання цього пакета; його розмір і SHA-256 записані в STOCK_FIRMWARE.md за результатом попереднього аудиту. Не робити нових висновків про вільні адреси без самого дампу.

