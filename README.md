# VortexTeletext (AmigaOS 4.1 PowerPC)

Nativer, eigenständiger **Live-Teletext-Client** für **AmigaOS 4.1 PowerPC** (ReAction GUI + AmiSSL 4/5 HTTPS), passend zur Vortex-Serie.

## Unterstützte Live-Sender
* **ARD** (`ard-text.de`, inkl. 40×25 Mosaik-Blockgrafik & Unterseiten)
* **ZDF** (`teletext.zdf.de`)
* **3SAT** (`teletext.3sat.de`)
* **HR** (`hr-text.hr-online.de`)
* **WDR** (`www1.wdr.de/wdrtext`)
* **SWR** (`wraps.swr.de/videotext`)

## Features
* **AmigaTText-inspirierte ReAction-GUI**:
  * Skalierbares 40×25 Teletext-Display (640×500 ARGB-Framebuffer mit Mosaik-Blockgrafik, Double-Height, Blinken und Reveal)
  * **Seiten Nummer** 4×4-Tastenfeld (`0–9`, `-1`, `+1`, `Gehe zu`, `Unterseite vor`, `Unterseite zurueck`)
  * **Seiten Nummer Auswahl** mit Direkteingabefeld, `Neu laden`, `Zoom` und Sender-Chooser (`ARD`, `ZDF`, `3SAT`, `HR`, `WDR`, `SWR`)
* **Cache & Navigation**:
  * Bereits geladene Seiten werden aus dem RAM-Cache angezeigt; **Neu laden** (`R` / `F5`) lädt frisch vom Sender
  * Klick auf jede 3-stellige Seitenzahl (`100–899`) im Teletext-Bild (mit Hover-Hervorhebung)
  * Tastatur: `0–9` Direkteingabe, `+`/`-` Seite hoch/runter, Pfeiltasten Navigation, `F1`–`F4` FastText, `Backspace` Verlauf zurück
* **Menü-Funktionen**: Volltextsuche (`Amiga+F`), Favoriten (`Amiga+B`, `ENVARC:VortexTeletext/`), Export als `.txt` (`Amiga+T`) oder `.bmp` (`Amiga+S`) nach `RAM:`

## Voraussetzungen auf AmigaOS 4.1
* AmigaOS 4.1 Update 2+ (PowerPC)
* AmiSSL v4 oder v5 (`amisslmaster.library`) für HTTPS-Abruf

## Kompilieren (Cross-Compile unter Linux)
```bash
./build.sh
```
Erzeugt `VortexTeletext` und `VortexTeletext.stripped` via Docker (`ppc-amigaos-gcc`).
