#pragma once

#include "stm32f4xx_hal.h"
#include "SSD1351_fonts.h"
#include <cstdint>
#include <cstddef>

/**
 * @file  ssd1351.h
 * @brief SSD1351 128×128 colour OLED driver for STM32F411 – SPI with optional DMA.
 *
 * This is a C++ class wrapper around the SSD1351 C library, modelled after the
 * SSD1306 HAL C++ driver for consistency.  All GPIO pin assignments (CS, DC, RES)
 * are injected via the constructor instead of being compile-time macros, so multiple
 * displays can coexist on the same SPI bus.
 *
 * The display communicates over SPI with three control lines:
 *  - CS  (Chip Select)   – active low
 *  - DC  (Data/Command)  – high = data, low = command
 *  - RES (Reset)         – active low
 *
 * DMA support
 * -----------
 * drawImageDMA() streams a pixel buffer to the display without blocking the CPU.
 * Wire the SPI TX-complete HAL callback into onTransferComplete():
 *
 * @code
 *  extern SSD1351 display;   // your global instance
 *
 *  void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
 *  {
 *      display.onTransferComplete(hspi);
 *  }
 * @endcode
 *
 * CubeMX setup (required for DMA)
 * --------------------------------
 *  1. Open the SPI peripheral → DMA Settings → Add TX stream.
 *     STM32F411 typical mappings:
 *       SPI1_TX  →  DMA2 Stream 3, Channel 3  (or Stream 5, Ch 3)
 *       SPI2_TX  →  DMA1 Stream 4, Channel 0
 *  2. Set DMA stream priority to at least Medium.
 *  3. Enable the DMA stream global interrupt in NVIC Settings.
 *  4. Enable the SPI global interrupt in NVIC Settings.
 */
class SSD1351 {
public:
    // ------------------------------------------------------------------ types

    // ---------------------------------------------------------------- colours
    /// Predefined RGB565 colour constants (same names as the original C macros).
    static constexpr uint16_t BLACK   = 0x0000;
    static constexpr uint16_t BLUE    = 0x001F;
    static constexpr uint16_t RED     = 0xF800;
    static constexpr uint16_t GREEN   = 0x07E0;
    static constexpr uint16_t CYAN    = 0x07FF;
    static constexpr uint16_t MAGENTA = 0xF81F;
    static constexpr uint16_t YELLOW  = 0xFFE0;
    static constexpr uint16_t WHITE   = 0xFFFF;

    /** @brief Pack 8-bit R, G, B components into a 16-bit RGB565 value. */
    static constexpr uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
    {
        return static_cast<uint16_t>(((r & 0xF8u) << 8) |
                                     ((g & 0xFCu) << 3) |
                                     ((b & 0xF8u) >> 3));
    }

    // --------------------------------------------------------------- constants
    static constexpr uint16_t WIDTH  = 128;
    static constexpr uint16_t HEIGHT = 128;

    // ------------------------------------------------------------ construction
    /**
     * @brief Construct the driver with all hardware handles baked in.
     *
     * @param hspi    HAL SPI handle.  Must have a DMA TX stream if drawImageDMA()
     *                is to be used.
     * @param csPort  GPIO port for the CS  pin (e.g. GPIOC).
     * @param csPin   GPIO pin mask for CS  (e.g. GPIO_PIN_0).
     * @param dcPort  GPIO port for the DC  pin.
     * @param dcPin   GPIO pin mask for DC.
     * @param resPort GPIO port for the RES pin.
     * @param resPin  GPIO pin mask for RES.
     */
    SSD1351(SPI_HandleTypeDef &hspi,
            GPIO_TypeDef *csPort,  uint16_t csPin,
            GPIO_TypeDef *dcPort,  uint16_t dcPin,
            GPIO_TypeDef *resPort, uint16_t resPin);

    // --------------------------------------------------------- initialisation
    /**
     * @brief  Drive the hardware reset sequence and send the full init command
     *         sequence (blocking).
     *
     * Call this once after power-on, before any other method.  CS is left
     * de-asserted (high) on return so other SPI peripherals may use the bus.
     *
     * @return true on success, false if any SPI transmit failed.
     */
    bool init();

    // --------------------------------------------------------- bus management
    /**
     * @brief De-assert CS so another SPI device can use the bus.
     *
     * Call this before initialising any other SPI peripheral on the same bus,
     * mirroring the original C library's SSD1351_Unselect().
     */
    void unselect();

    // --------------------------------------------------------- drawing helpers
    /**
     * @brief  Draw a single pixel at (x, y) in the given RGB565 colour.
     *
     * The function asserts/de-asserts CS around the transaction.
     */
    void drawPixel(uint16_t x, uint16_t y, uint16_t color);

    /**
     * @brief  Fill a rectangular region with a solid colour (blocking).
     *
     * Coordinates are clipped to the display boundaries.
     *
     * @param x, y   Top-left corner.
     * @param w, h   Width and height in pixels.
     * @param color  RGB565 fill colour.
     */
    void fillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

    /**
     * @brief  Fill the entire 128×128 display with a single colour (blocking).
     *
     * Equivalent to fillRectangle(0, 0, WIDTH, HEIGHT, color).
     * Named fill() to match the SSD1306 C++ driver convention.
     */
    void fill(uint16_t color);

    /**
     * @brief  Blit a pre-encoded RGB565 image to the display (blocking).
     *
     * @param x, y  Top-left destination corner.
     * @param w, h  Width and height of the image in pixels.
     * @param data  Pointer to w×h uint16_t values in RGB565 format, row-major.
     *
     * The function returns without drawing if the image would exceed the display
     * boundaries.
     */
    void drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   const uint16_t *data);

    /**
     * @brief  Blit a pre-encoded RGB565 image using SPI DMA (non-blocking).
     *
     * The function sets the address window, then hands the pixel buffer to
     * HAL_SPI_Transmit_DMA().  It returns immediately; the hardware completes
     * the transfer in the background.  CS is de-asserted automatically inside
     * onTransferComplete() once the ISR fires.
     *
     * @note   Returns false immediately (without starting a transfer) if a
     *         previous DMA operation has not yet completed.  Poll isBusy() or
     *         wait for onTransferComplete() before calling again.
     *
     * @return true  DMA transfer successfully queued.
     * @return false Peripheral busy, dimensions out of bounds, or HAL error.
     */
    bool drawImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint16_t *data);

    // ------------------------------------------------------- DMA state query
    /** @return true while a DMA transfer is in progress. */
    bool isBusy() const { return _dmaInProgress; }

    /**
     * @brief  De-assert CS and clear the busy flag once the SPI DMA ISR fires.
     *
     * Wire this into HAL_SPI_TxCpltCallback() – see the header-level example.
     *
     * @param hspi  Pointer provided by the HAL callback; used to match the
     *              correct peripheral when multiple SPI buses are in use.
     */
    void onTransferComplete(SPI_HandleTypeDef *hspi);

    // ----------------------------------------------------------- text helpers
    /**
     * @brief  Render a single character at (x, y).
     *
     * @param x, y    Top-left pixel of the character cell.
     * @param ch      Character to draw (ASCII 32–127).
     * @param font    Font descriptor.
     * @param color   Foreground RGB565 colour.
     * @param bgcolor Background RGB565 colour.
     * @return The character on success, 0 if it would exceed the display edge.
     */
    char writeChar(uint16_t x, uint16_t y, char ch, FontDef font,
                   uint16_t color, uint16_t bgcolor);

    /**
     * @brief  Render a null-terminated string starting at (x, y), with
     *         automatic line-wrap at the right edge of the display.
     *
     * @return The character that caused a failure (out of screen), or '\\0' on
     *         complete success.
     */
    char writeString(uint16_t x, uint16_t y, const char *str, FontDef font,
                     uint16_t color, uint16_t bgcolor);

    /**
     * @brief  Move the internal text cursor to (x, y) in pixels.
     *
     * The cursor is used by the zero-argument overloads of writeChar /
     * writeString (below).  It is also advanced automatically after each
     * character is drawn by those overloads.
     */
    void setCursor(uint16_t x, uint16_t y);

    /**
     * @brief  Render a character at the current cursor position and advance
     *         the cursor by the character width.
     *
     * Mirrors the SSD1306 writeChar() signature exactly.
     *
     * @return The character on success, 0 if it would exceed the display edge.
     */
    char writeChar(char ch, FontDef font, uint16_t color, uint16_t bgcolor);

    /**
     * @brief  Render a string at the current cursor position with line-wrap.
     *
     * Mirrors the SSD1306 writeString() signature exactly.
     *
     * @return The failing character, or '\\0' on complete success.
     */
    char writeString(const char *str, FontDef font,
                     uint16_t color, uint16_t bgcolor);

    // ---------------------------------------------------- hardware inversion
    /**
     * @brief  Toggle hardware colour inversion via SSD1351 command.
     *
     * Unlike the SSD1306 software inversion, this operates entirely in the
     * display controller and does not alter the framebuffer or cursor state.
     *
     * @param invert  true  → INVERTDISPLAY (0xA7)
     *                false → NORMALDISPLAY  (0xA6)
     */
    void invertColors(bool invert);

private:
    // --------------------------------------------------------- private helpers
    void select();
    void reset();
    void writeCommand(uint8_t cmd);
    void writeData(const uint8_t *data, size_t size);
    void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    // -------------------------------------------------------------- SPI state
    SPI_HandleTypeDef &_hspi;

    GPIO_TypeDef *_csPort;
    uint16_t      _csPin;
    GPIO_TypeDef *_dcPort;
    uint16_t      _dcPin;
    GPIO_TypeDef *_resPort;
    uint16_t      _resPin;

    // --------------------------------------------------------------- cursor
    uint16_t _cursorX = 0;
    uint16_t _cursorY = 0;

    /**
     * Set to true when a DMA transfer is in flight; cleared by
     * onTransferComplete().  Declared volatile because it is written from
     * inside an ISR context (via the HAL callback).
     */
    volatile bool _dmaInProgress = false;
};
