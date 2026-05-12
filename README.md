# stm32-ssd1351-cpp

SSD1351 128×128 colour OLED driver for STM32F411, written as a C++ class with HAL SPI and optional DMA — modelled after the companion [ssd1306-stm32HAL-cpp](https://github.com/) driver.

> **AI-assisted refactor**
> This library was ported from the original C driver ([afiskon/stm32-ssd1351](https://github.com/afiskon/stm32-ssd1351)) to C++ with assistance from [Claude](https://claude.ai) (Anthropic).
> The AI performed the mechanical conversion — class wrapping, constructor injection, DMA pattern, naming convention alignment — guided by a human-authored specification and reviewed against a reference C++ driver ([ssd1306-stm32HAL-cpp](https://github.com/)) written by the same author.
> All generated code was reviewed by the project maintainer before publication.

---

## Files

| File | Description |
|------|-------------|
| `Inc/ssd1351.h` | Class declaration, colour constants, Doxygen API docs |
| `Src/ssd1351.cpp` | Full implementation |
| `Inc/ssd1351_fonts.h` | `SSD1351_FontDef` struct and font declarations |
| `Src/ssd13511_fonts.c` | Font bitmaps (7×10, 11×18, 16×26) |

---

## Hardware wiring

The SSD1351 uses a 4-wire SPI interface plus two control lines:

| Signal | Direction | Description |
|--------|-----------|-------------|
| SPI MOSI | MCU → display | Serial data |
| SPI SCK  | MCU → display | Clock |
| CS       | MCU → display | Chip select (active-low) |
| DC       | MCU → display | Data (high) / Command (low) |
| RES      | MCU → display | Hardware reset (active-low) |

---

## CubeMX setup

### Basic SPI (blocking)
1. Enable an SPI peripheral in **Full-Duplex Master** mode, **8-bit data size**.
2. Configure the three GPIO output pins (CS, DC, RES) as `GPIO_OUTPUT_PP`.
3. No DMA or interrupts needed for blocking operation.

### With DMA (for `drawImageDMA()`)
1. Open the SPI peripheral → **DMA Settings** → add a TX stream.
   Typical STM32F411 mappings:

   | Peripheral | Stream | Channel |
   |------------|--------|---------|
   | SPI1_TX | DMA2 Stream 3 | Channel 3 |
   | SPI1_TX | DMA2 Stream 5 | Channel 3 |
   | SPI2_TX | DMA1 Stream 4 | Channel 0 |

2. Set DMA stream priority to at least **Medium**.
3. Enable the **DMA stream global interrupt** in NVIC Settings.
4. Enable the **SPI global interrupt** in NVIC Settings.

---

## Usage

### Instantiation

```cpp
#include "ssd1351.h"

// Declared once — usually as a global or static
SSD1351 display(hspi1,
                GPIOC, GPIO_PIN_0,   // CS
                GPIOC, GPIO_PIN_1,   // DC
                GPIOB, GPIO_PIN_0);  // RES
```

### Initialisation

```cpp
// Release the bus before any other SPI device initialises
display.unselect();

// Hardware reset + full init command sequence
display.init();
```

### Drawing

```cpp
// Solid fill
display.fill(SSD1351::BLACK);

// Single pixel
display.drawPixel(10, 20, SSD1351::RED);

// Filled rectangle
display.fillRectangle(0, 0, 64, 64, SSD1351::BLUE);

// Custom colour
uint16_t orange = SSD1351::color565(255, 165, 0);
display.fillRectangle(64, 0, 64, 64, orange);
```

### Text — explicit position

```cpp
display.writeString(0,  0, "Hello!", SSD1351_Font_7x10,  SSD1351::WHITE,  SSD1351::BLACK);
display.writeString(0, 20, "STM32",  SSD1351_Font_11x18, SSD1351::YELLOW, SSD1351::BLACK);
```

### Text — cursor-based (matches SSD1306 API)

```cpp
display.setCursor(0, 0);
display.writeString("Hello!", SSD1351_Font_7x10, SSD1351::WHITE, SSD1351::BLACK);

display.setCursor(0, 20);
display.writeChar('A', SSD1351_Font_11x18, SSD1351::GREEN, SSD1351::BLACK);
```

### Image blit — blocking

```cpp
extern const uint16_t myImage[128 * 128];   // RGB565, row-major
display.drawImage(0, 0, 128, 128, myImage);
```

### Image blit — DMA (non-blocking)

Wire `onTransferComplete` into the SPI TX-complete callback. The callback must be
reachable from C, so it belongs in `stm32f4xx_it.c` or in an `extern "C"` block.

**Option A — in `stm32f4xx_it.c` (pure C file)**

Add a forward declaration and the callback body:

```c
/* stm32f4xx_it.c */

/* Forward declaration — defined in app_main.cpp or similar */
void SSD1351_TxCpltCallback(SPI_HandleTypeDef *hspi);

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    SSD1351_TxCpltCallback(hspi);
}
```

Then in your C++ application file:

```cpp
// app_main.cpp
extern SSD1351 display;

extern "C" void SSD1351_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    display.onTransferComplete(hspi);
}
```

**Option B — entirely in a C++ file with `extern "C"`**

If your callback is defined in your main C++ file, use an `extern "C"` block:

```cpp
// app_main.cpp
extern "C" {
    void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
        display.onTransferComplete(hspi);
    }
}
```

**Using `drawImageDMA`:**

```cpp
// The image buffer must remain valid until onTransferComplete() fires.
// Do NOT pass a local (stack) variable.
static const uint16_t myImage[128 * 128] = { /* ... */ };

if (!display.isBusy())
    display.drawImageDMA(0, 0, 128, 128, myImage);
```

### Colour inversion (hardware)

```cpp
display.invertColors(true);   // INVERTDISPLAY
display.invertColors(false);  // NORMALDISPLAY
```

---

## API reference

### Constructor

```cpp
SSD1351(SPI_HandleTypeDef &hspi,
        GPIO_TypeDef *csPort,  uint16_t csPin,
        GPIO_TypeDef *dcPort,  uint16_t dcPin,
        GPIO_TypeDef *resPort, uint16_t resPin);
```

### Methods

| Method | Description |
|--------|-------------|
| `bool init()` | Hardware reset + init command sequence |
| `void unselect()` | De-assert CS (call before other SPI devices init) |
| `void fill(color)` | Fill entire screen |
| `void fillRectangle(x, y, w, h, color)` | Fill rectangle (clipped) |
| `void drawPixel(x, y, color)` | Draw single pixel |
| `void drawImage(x, y, w, h, data)` | Blit RGB565 image (blocking) |
| `bool drawImageDMA(x, y, w, h, data)` | Blit RGB565 image via DMA (non-blocking) |
| `bool isBusy()` | True while a DMA transfer is in progress |
| `void onTransferComplete(hspi)` | Call from `HAL_SPI_TxCpltCallback` |
| `char writeChar(x, y, ch, font, color, bgcolor)` | Draw char at explicit position |
| `char writeString(x, y, str, font, color, bgcolor)` | Draw string at explicit position |
| `void setCursor(x, y)` | Set text cursor |
| `char writeChar(ch, font, color, bgcolor)` | Draw char at cursor (advances cursor) |
| `char writeString(str, font, color, bgcolor)` | Draw string at cursor |
| `void invertColors(bool invert)` | Hardware colour inversion |

### Colour constants

```cpp
SSD1351::BLACK    SSD1351::WHITE    SSD1351::RED      SSD1351::GREEN
SSD1351::BLUE     SSD1351::CYAN     SSD1351::MAGENTA  SSD1351::YELLOW
SSD1351::color565(r, g, b)   // pack custom RGB into RGB565
```

### Font objects

Declared in `ssd1351_fonts.h`, defined in `ssd13511_fonts.c`:

```cpp
SSD1351_Font_7x10    // small  — fits ~18 chars across the 128 px width
SSD1351_Font_11x18   // medium — fits ~11 chars across
SSD1351_Font_16x26   // large  — fits ~8 chars across
```

---

## DMA notes

- **Buffer lifetime.** The pixel buffer passed to `drawImageDMA()` must remain in
  memory until `onTransferComplete()` fires (i.e. until `isBusy()` returns false).
  Never pass a stack-allocated buffer.
- **No concurrent calls.** `drawImageDMA()` returns `false` immediately if a
  previous transfer has not yet completed. Check `isBusy()` before calling.
- **Callback wiring is mandatory.** Without `onTransferComplete()` being called,
  `_dmaInProgress` is never cleared and all subsequent `drawImageDMA()` calls
  will silently do nothing.
- **SPI data size must be 8-bit.** The driver sends pixel data as raw bytes. If
  your CubeMX SPI configuration uses 16-bit data size, change it to 8-bit.

---

## Key differences from the original C library

| Feature | Original C | This C++ library |
|---------|------------|-----------------|
| Configuration | Compile-time `#define` macros | Constructor parameters |
| Multiple displays | Not supported | One instance per display |
| DMA support | None | `drawImageDMA()` + callback |
| Cursor-based text | None | `setCursor()` + cursor overloads |
| `fill()` alias | `SSD1351_FillScreen()` | `fill()` (matches SSD1306) |
| CS management | Global select/unselect functions | Per-instance, automatic |

---

## License

MIT — same as the original [stm32-ssd1351](https://github.com/afiskon/stm32-ssd1351) C library.

## Acknowledgements

- Original C driver by [afiskon](https://github.com/afiskon/stm32-ssd1351)
- C++ refactor assisted by [Claude](https://claude.ai) (Anthropic, claude-sonnet-4-6)
