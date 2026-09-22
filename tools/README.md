# Werkzeuge

## `re/` – Analyse eines ESP32-App-Images

| Datei | Zweck |
|---|---|
| `img2elf.py app.bin app.elf` | App-Image → ELF (ein PT_LOAD pro Segment) für Ghidra |
| `readmem.py app.bin 0xADDR …` | 32-Bit-Wörter an virtuellen Adressen lesen |
| `findlit.py app.bin 0x800001c 9600` | Literal-Pool-Adressen zu Konstanten finden (→ `DAT_xxxxxxxx` in Ghidra) |
| `sdm_table.py app.bin` | Kinetos-MID-Registertabelle ausgeben |
| `scripts/ExportDecomp.java` | Ghidra headless: alle Funktionen dekompilieren |
| `scripts/DecompAt.java`, `decompat.sh` | Funktionen an Adressen anlegen und dekompilieren |

```bash
pip install esptool
python3 img2elf.py app0.bin app.elf
analyzeHeadless ./proj ghidra -import app.elf -processor "Xtensa:LE:32:default" \
  -scriptPath scripts -postScript ExportDecomp.java $PWD/decomp.c
```

## `modbus/` – smart1 (Modbus TCP)

```bash
pip install pymodbus
python3 smart1_read.py WALLBOX      # Status-, Mess- und Steuerblöcke lesen
python3 smart1_mbscan.py WALLBOX    # belegte Register finden
```
