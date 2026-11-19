**Flash** over USB · `-3c` is the tri-colour GDEY042Z98 panel, `-bw` the black-and-white GYE042A87. Release builds have no OTA password, so over-the-air updates stay off; build from source with `gen-secrets.sh` to get them.

```bash
esptool.py --chip esp32s3 write_flash 0x0 claudometer-<version>-3c.bin
```

On first boot the board opens the **Quota-Setup** Wi-Fi (password `quotaboard`): set Wi-Fi at http://192.168.4.1, then turn on **Settings → Quota board** in [Agent Meter](https://github.com/kpeeem/agent-meter).

**Прошивка** по USB · `-3c` — трёхцветная панель, `-bw` — чёрно-белая. В релизной сборке нет пароля OTA, поэтому обновление по воздуху выключено; для OTA собери прошивку сам через `gen-secrets.sh`. После прошивки подключись к Wi-Fi **Quota-Setup** (пароль `quotaboard`), настрой сеть на http://192.168.4.1 и включи табло в Agent Meter.
