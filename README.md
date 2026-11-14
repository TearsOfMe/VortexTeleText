# VortexTeletext (AmigaOS 4.1 PowerPC)

*[English](#english) | [Deutsch](#deutsch)*

---

<a id="english"></a>
## English

A native, standalone **Live Teletext Client** for **AmigaOS 4.1 PowerPC** (ReAction GUI + AmiSSL 4/5 HTTPS), inspired by **AmigaTText** and part of the Vortex suite.

### Supported Live Channels
* **ARD** (`ard-text.de` — native 40×25 mosaic block graphics & subpages)
* **ZDF** (`teletext.zdf.de`)
* **3SAT** (`teletext.zdf.de`)
* **HR** (`hr-text.hr-fernsehen.de`)
* **WDR** (`www1.wdr.de/wdrtext`)
* **SWR RP** (`wraps.swr.de/videotext/?stream=rp`)
* **SWR BW** (`wraps.swr.de/videotext/?stream=bw`)
* **SR (Saartext)** (`saartext.de`)

### Features & Controls
* **AmigaTText-Style ReAction GUI**:
  * **40×25 Teletext Display**: 640×500 ARGB framebuffer with contiguous/separated mosaic block graphics, double-height text, flashing, and hidden text (Reveal). Automatically scales cleanly when resizing or zooming.
  * **"Seiten Nummer" 4×4 Keypad**:
    * Digits `0–9` with live input display (automatically loads once 3 digits are entered)
    * `Gehe zu`: jumps to the entered page number
    * `-1` / `+1`: previous / next page
    * `Unterseite vor` / `Unterseite zurueck`: cycle through subpages
  * **"Seiten Nummer Auswahl" Bar**:
    * Direct page entry field (press `Enter` to load)
    * `Neu laden`: forces a fresh live download from the internet (bypassing the RAM cache)
    * Channel chooser (`ARD`, `ZDF`, `3SAT`, `HR`, `WDR`, `SWR RP`, `SWR BW`, `SR`)
* **Mouse & Keyboard Shortcuts**:
  * **Mouse Wheel**: smooth zoom (`100%–300%`) while hovering over the Teletext display (Middle-click or `Amiga+0` resets zoom)
  * **Fit to Window Width** (`Funktionen -> An Fensterbreite anpassen`): scales the Teletext display to the exact window width (enabled by default, saved in prefs)
  * **Mouse Click**: click any 3-digit page number (`100–899`) directly inside the Teletext page (highlighted on hover)
  * `0–9`: direct 3-digit page entry
  * `+` / `-`: page up / page down
  * `Cursor Left` / `Right`: previous / next page
  * `Cursor Up` / `Down`: next / previous subpage
  * `R` / `F5`: force reload current page
  * `F1`–`F4`: FastText colored links (Red, Green, Yellow, Cyan)
  * `Backspace`: navigate back in history
* **Menu Functions** (`Projekt`, `Navigation`, `Funktionen`):
  * **RAM Cache**: previously loaded pages open instantly from memory without re-downloading
  * **Full-Text Search** (`Amiga+F`): search across all cached pages
  * **Favorites** (`Amiga+B`): persistent bookmarks stored in `ENVARC:VortexTeletext/`
  * **Export**: save current page as plain text `.txt` (`Amiga+T`) or 24-bit `.bmp` image (`Amiga+S`) to `RAM:`
  * **Reveal** (`Amiga+H`) & **Flash toggle**

### Requirements (AmigaOS 4.1)
* AmigaOS 4.1 Update 2 or newer (PowerPC)
* AmiSSL v4 or v5 (`amisslmaster.library`) for HTTPS connections
* Active internet connection

### Installation (AmigaOS 4.1)
1. Copy `VortexTeletext` and `VortexTeletext.info` to any drawer on your Amiga (e.g. `Work:VortexTeletext/`).
2. Make sure AmiSSL is installed.
3. Double-click the `VortexTeletext` icon to start.

### Building from Source (Linux Cross-Compile)
Requires Docker with the `ppc-amigaos-gcc` cross-compiler toolchain:
```bash
./build.sh
```
This builds `VortexTeletext` and `VortexTeletext.stripped`.

---

<a id="deutsch"></a>
## Deutsch

Ein nativer, eigenständiger **Live-Teletext-Client** für **AmigaOS 4.1 PowerPC** (ReAction GUI + AmiSSL 4/5 HTTPS), inspiriert von **AmigaTText** und passend zur Vortex-Serie.

### Unterstützte Live-Sender
* **ARD** (`ard-text.de` – inkl. 40×25 Mosaik-Blockgrafik & Unterseiten)
* **ZDF** (`teletext.zdf.de`)
* **3SAT** (`teletext.zdf.de`)
* **HR** (`hr-text.hr-fernsehen.de`)
* **WDR** (`www1.wdr.de/wdrtext`)
* **SWR RP** (`wraps.swr.de/videotext/?stream=rp`)
* **SWR BW** (`wraps.swr.de/videotext/?stream=bw`)
* **SR (Saartext)** (`saartext.de`)

### Funktionen & Bedienung
* **ReAction-GUI im AmigaTText-Stil**:
  * **40×25 Teletext-Anzeige**: 640×500 ARGB-Framebuffer mit Mosaik-Blockgrafiken, doppelter Texthöhe (Double-Height), Blinken (Flash) und verdecktem Text (Reveal). Skaliert sauber beim Vergrößern oder Zoomen.
  * **„Seiten Nummer“ 4×4-Tastenfeld**:
    * Ziffern `0–9` mit Eingabeanzeige (lädt automatisch nach 3 eingegebenen Ziffern)
    * `Gehe zu`: springt zur eingegebenen Seitenzahl
    * `-1` / `+1`: vorherige / nächste Seite
    * `Unterseite vor` / `Unterseite zurueck`: durch Unterseiten blättern
  * **„Seiten Nummer Auswahl“**:
    * Direkteingabefeld für Seitenzahlen (`Enter` lädt die Seite)
    * `Neu laden`: lädt die aktuelle Seite frisch aus dem Internet (umgeht den RAM-Cache)
    * Sender-Auswahl (`ARD`, `ZDF`, `3SAT`, `HR`, `WDR`, `SWR RP`, `SWR BW`, `SR`)
* **Maus- & Tastatursteuerung**:
  * **Mausrad**: stufenloser Zoom (`100%–300%`) über der Teletext-Anzeige (Mittelklick oder `Amiga+0` setzt Zoom zurück)
  * **An Fensterbreite anpassen** (`Funktionen -> An Fensterbreite anpassen`): skaliert die Teletext-Anzeige exakt auf die Fensterbreite (Standard aktiv, gespeichert in Prefs)
  * **Mausklick**: jede 3-stellige Seitenzahl (`100–899`) im Teletext-Bild ist direkt anklickbar (mit Hover-Hervorhebung)
  * `0–9`: direkte 3-stellige Seiteneingabe
  * `+` / `-`: Seite hoch / runter
  * `Pfeil Links` / `Rechts`: vorherige / nächste Seite
  * `Pfeil Hoch` / `Runter`: nächste / vorherige Unterseite
  * `R` / `F5`: aktuelle Seite neu laden
  * `F1`–`F4`: FastText-Farbtasten (Rot, Grün, Gelb, Cyan)
  * `Backspace`: im Verlauf zurückspringen
* **Menü-Funktionen** (`Projekt`, `Navigation`, `Funktionen`):
  * **RAM-Cache**: bereits geladene Seiten werden sofort aus dem Speicher angezeigt, ohne erneut heruntergeladen zu werden
  * **Volltextsuche** (`Amiga+F`): durchsucht alle zwischengespeicherten Seiten
  * **Favoriten** (`Amiga+B`): dauerhafte Lesezeichen unter `ENVARC:VortexTeletext/`
  * **Export**: aktuelle Seite als Textdatei `.txt` (`Amiga+T`) oder 24-Bit-Bild `.bmp` (`Amiga+S`) nach `RAM:` speichern
  * **Verdeckten Text aufdecken / Reveal** (`Amiga+H`) & **Blinken umschalten**

### Voraussetzungen (AmigaOS 4.1)
* AmigaOS 4.1 Update 2 oder neuer (PowerPC)
* AmiSSL v4 oder v5 (`amisslmaster.library`) für HTTPS-Verbindungen
* Aktive Internetverbindung

### Installation auf AmigaOS 4.1
1. `VortexTeletext` und `VortexTeletext.info` in ein beliebiges Verzeichnis auf dem Amiga kopieren (z. B. `Work:VortexTeletext/`).
2. Sicherstellen, dass AmiSSL installiert ist.
3. Zum Starten einfach das `VortexTeletext`-Icon doppelklicken.

### Kompilieren (Cross-Compile unter Linux)
Benötigt Docker mit der `ppc-amigaos-gcc`-Toolchain:
```bash
./build.sh
```
Erzeugt `VortexTeletext` und `VortexTeletext.stripped`.
