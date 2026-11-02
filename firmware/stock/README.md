# Stock firmware images

Put additional stock Intel HEX images in this folder (`firmware/stock/`). The
single-file app builder discovers every `*.hex` here on each build, validates
the HEX structure and flash range, and embeds the images automatically.

The list sorts by firmware version, newest first. Equal versions put `EN` first;
the remaining filenames sort alphabetically. Use filenames containing a
semantic version such as `EN-4.2.0.hex` so the build can order them correctly.

The updater checks the Milerd firmware APIs monthly and can also be started from
Actions → **Check stock firmware updates** → **Run workflow**. New validated
images are proposed in a pull request. `sources.json` records the API route,
download URL, version, and SHA-256 for images obtained through that updater.
It also supplies the shared source-link list shown under the stock firmware
buttons in the app.
The four initial images remain as supplied; two of them match the current API
downloads byte for byte. The English `E` and `T` channels are checked through
`milerdport.cloud`; the Russian `u` channel is checked through
`mainserver.milerd.ru`. The initial RU metadata points to version 3.0.19; its
HEX is already in the catalog. The worker records its API provenance and
verified file hash after it can retrieve that download.
