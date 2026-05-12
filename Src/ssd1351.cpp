#include "ssd1351.h"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SSD1351::SSD1351(SPI_HandleTypeDef &hspi,
                 GPIO_TypeDef *csPort,  uint16_t csPin,
                 GPIO_TypeDef *dcPort,  uint16_t dcPin,
                 GPIO_TypeDef *resPort, uint16_t resPin)
    : _hspi(hspi),
      _csPort(csPort), _csPin(csPin),
      _dcPort(dcPort), _dcPin(dcPin),
      _resPort(resPort), _resPin(resPin)
{}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SSD1351::select()
{
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
}

void SSD1351::unselect()
{
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
}

void SSD1351::reset()
{
    HAL_GPIO_WritePin(_resPort, _resPin, GPIO_PIN_SET);
    HAL_Delay(500);
    HAL_GPIO_WritePin(_resPort, _resPin, GPIO_PIN_RESET);
    HAL_Delay(500);
    HAL_GPIO_WritePin(_resPort, _resPin, GPIO_PIN_SET);
    HAL_Delay(500);
}

void SSD1351::writeCommand(uint8_t cmd)
{
    HAL_GPIO_WritePin(_dcPort, _dcPin, GPIO_PIN_RESET);  // DC low = command
    HAL_SPI_Transmit(&_hspi, &cmd, sizeof(cmd), HAL_MAX_DELAY);
}

void SSD1351::writeData(const uint8_t *data, size_t size)
{
    HAL_GPIO_WritePin(_dcPort, _dcPin, GPIO_PIN_SET);    // DC high = data

    // Split into ≤32 768-byte chunks: HAL_SPI_Transmit size argument is
    // uint16_t, so it cannot represent transfers larger than 65 535 bytes.
    // 32 768 is a safe power-of-two upper bound.
    while (size > 0)
    {
        uint16_t chunk = (size > 32768u) ? 32768u : static_cast<uint16_t>(size);
        HAL_SPI_Transmit(&_hspi, const_cast<uint8_t *>(data), chunk, HAL_MAX_DELAY);
        data += chunk;
        size -= chunk;
    }
}

void SSD1351::setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // Column address (SETCOLUMN)
    writeCommand(0x15);
    {
        uint8_t d[] = { static_cast<uint8_t>(x0 & 0xFF),
                        static_cast<uint8_t>(x1 & 0xFF) };
        writeData(d, sizeof(d));
    }

    // Row address (SETROW)
    writeCommand(0x75);
    {
        uint8_t d[] = { static_cast<uint8_t>(y0 & 0xFF),
                        static_cast<uint8_t>(y1 & 0xFF) };
        writeData(d, sizeof(d));
    }

    // Write to RAM
    writeCommand(0x5C); // WRITERAM
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool SSD1351::init()
{
    select();
    reset();

    // Command sequence mirrored from the Adafruit SSD1351 library, preserved
    // from the original C driver.

    writeCommand(0xFD); // COMMANDLOCK – unlock commands
    { uint8_t d[] = { 0x12 }; writeData(d, sizeof(d)); }

    writeCommand(0xFD); // COMMANDLOCK – unlock all other commands
    { uint8_t d[] = { 0xB1 }; writeData(d, sizeof(d)); }

    writeCommand(0xAE); // DISPLAYOFF

    writeCommand(0xB3); // CLOCKDIV
    writeCommand(0xF1); // 7:4 = oscillator freq, 3:0 = CLK div ratio (A[3:0]+1)

    writeCommand(0xCA); // MUXRATIO
    { uint8_t d[] = { 0x7F }; writeData(d, sizeof(d)); }   // 127

    writeCommand(0xA0); // SETREMAP – colour depth 65 K, enable COM split
    { uint8_t d[] = { 0x74 }; writeData(d, sizeof(d)); }

    writeCommand(0x15); // SETCOLUMN
    { uint8_t d[] = { 0x00, 0x7F }; writeData(d, sizeof(d)); }

    writeCommand(0x75); // SETROW
    { uint8_t d[] = { 0x00, 0x7F }; writeData(d, sizeof(d)); }

    writeCommand(0xA1); // STARTLINE
    { uint8_t d[] = { 0x00 }; writeData(d, sizeof(d)); }   // 96 if height == 96

    writeCommand(0xA2); // DISPLAYOFFSET
    { uint8_t d[] = { 0x00 }; writeData(d, sizeof(d)); }

    writeCommand(0xB5); // SETGPIO
    { uint8_t d[] = { 0x00 }; writeData(d, sizeof(d)); }

    writeCommand(0xAB); // FUNCTIONSELECT
    { uint8_t d[] = { 0x01 }; writeData(d, sizeof(d)); }   // internal VDD regulator

    writeCommand(0xB1); // PRECHARGE
    { uint8_t d[] = { 0x32 }; writeData(d, sizeof(d)); }

    writeCommand(0xBE); // VCOMH
    { uint8_t d[] = { 0x05 }; writeData(d, sizeof(d)); }

    writeCommand(0xA6); // NORMALDISPLAY (not inverted)

    writeCommand(0xC1); // CONTRASTABC
    { uint8_t d[] = { 0xC8, 0x80, 0xC8 }; writeData(d, sizeof(d)); }

    writeCommand(0xC7); // CONTRASTMASTER
    { uint8_t d[] = { 0x0F }; writeData(d, sizeof(d)); }

    writeCommand(0xB4); // SETVSL
    { uint8_t d[] = { 0xA0, 0xB5, 0x55 }; writeData(d, sizeof(d)); }

    writeCommand(0xB6); // PRECHARGE2
    { uint8_t d[] = { 0x01 }; writeData(d, sizeof(d)); }

    writeCommand(0xAF); // DISPLAYON

    unselect();
    return true;    // All commands are fire-and-forget; no HAL status to check
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void SSD1351::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= WIDTH || y >= HEIGHT)
        return;

    select();

    setAddressWindow(x, y, x + 1, y + 1);
    uint8_t d[] = { static_cast<uint8_t>(color >> 8),
                    static_cast<uint8_t>(color & 0xFF) };
    writeData(d, sizeof(d));

    unselect();
}

void SSD1351::fillRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             uint16_t color)
{
    // Clipping
    if (x >= WIDTH || y >= HEIGHT) return;
    if (x + w > WIDTH)  w = WIDTH  - x;
    if (y + h > HEIGHT) h = HEIGHT - y;

    select();
    setAddressWindow(x, y, x + w - 1, y + h - 1);

    // Pre-encode the two colour bytes once, then blast them pixel-by-pixel.
    uint8_t d[] = { static_cast<uint8_t>(color >> 8),
                    static_cast<uint8_t>(color & 0xFF) };

    HAL_GPIO_WritePin(_dcPort, _dcPin, GPIO_PIN_SET);   // DC high = data
    const uint32_t total = static_cast<uint32_t>(w) * h;
    for (uint32_t i = 0; i < total; ++i)
        HAL_SPI_Transmit(&_hspi, d, sizeof(d), HAL_MAX_DELAY);

    unselect();
}

void SSD1351::fill(uint16_t color)
{
    fillRectangle(0, 0, WIDTH, HEIGHT, color);
}

void SSD1351::drawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint16_t *data)
{
    if (x >= WIDTH  || y >= HEIGHT)    return;
    if (x + w > WIDTH  || y + h > HEIGHT) return;   // reject out-of-bounds images

    select();
    setAddressWindow(x, y, x + w - 1, y + h - 1);
    writeData(reinterpret_cast<const uint8_t *>(data),
              static_cast<size_t>(w) * h * sizeof(uint16_t));
    unselect();
}

// ---------------------------------------------------------------------------
// DMA image blit (non-blocking)
// ---------------------------------------------------------------------------

bool SSD1351::drawImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           const uint16_t *data)
{
    if (_dmaInProgress)                return false;
    if (x >= WIDTH  || y >= HEIGHT)    return false;
    if (x + w > WIDTH || y + h > HEIGHT) return false;

    select();
    setAddressWindow(x, y, x + w - 1, y + h - 1);

    HAL_GPIO_WritePin(_dcPort, _dcPin, GPIO_PIN_SET);   // DC high = data

    _dmaInProgress = true;

    const uint16_t pixelCount = w * h;
    if (HAL_SPI_Transmit_DMA(&_hspi,
                              reinterpret_cast<uint8_t *>(const_cast<uint16_t *>(data)),
                              pixelCount * sizeof(uint16_t)) != HAL_OK)
    {
        _dmaInProgress = false;
        unselect();
        return false;
    }

    // CS is released inside onTransferComplete() once the ISR fires.
    return true;
}

// ---------------------------------------------------------------------------
// DMA transfer-complete callback
// ---------------------------------------------------------------------------

void SSD1351::onTransferComplete(SPI_HandleTypeDef *hspi)
{
    if (hspi == &_hspi)
    {
        unselect();
        _dmaInProgress = false;
    }
}

// ---------------------------------------------------------------------------
// Text rendering – explicit (x, y) overloads
// ---------------------------------------------------------------------------

char SSD1351::writeChar(uint16_t x, uint16_t y, char ch, SSD1351_FontDef font,
                        uint16_t color, uint16_t bgcolor)
{
    if (x + font.width  > WIDTH  ||
        y + font.height > HEIGHT)
        return 0;

    setAddressWindow(x, y, x + font.width - 1, y + font.height - 1);

    for (uint32_t row = 0; row < font.height; ++row)
    {
        uint16_t bits = font.data[static_cast<uint8_t>(ch - 32) * font.height + row];

        for (uint32_t col = 0; col < font.width; ++col)
        {
            uint16_t px = ((bits << col) & 0x8000u) ? color : bgcolor;
            uint8_t  d[] = { static_cast<uint8_t>(px >> 8),
                             static_cast<uint8_t>(px & 0xFF) };
            writeData(d, sizeof(d));
        }
    }

    return ch;
}

char SSD1351::writeString(uint16_t x, uint16_t y, const char *str, SSD1351_FontDef font,
                          uint16_t color, uint16_t bgcolor)
{
    select();

    while (*str)
    {
        if (x + font.width >= WIDTH)
        {
            x  = 0;
            y += font.height;
            if (y + font.height >= HEIGHT)
                break;

            if (*str == ' ')    // skip leading space on wrapped line
            {
                ++str;
                continue;
            }
        }

        if (writeChar(x, y, *str, font, color, bgcolor) != *str)
        {
            unselect();
            return *str;
        }

        x   += font.width;
        ++str;
    }

    unselect();
    return '\0';
}

// ---------------------------------------------------------------------------
// Text rendering – cursor-based overloads (mirrors SSD1306 API)
// ---------------------------------------------------------------------------

void SSD1351::setCursor(uint16_t x, uint16_t y)
{
    _cursorX = x;
    _cursorY = y;
}

char SSD1351::writeChar(char ch, SSD1351_FontDef font, uint16_t color, uint16_t bgcolor)
{
    select();
    char result = writeChar(_cursorX, _cursorY, ch, font, color, bgcolor);
    unselect();

    if (result)
        _cursorX += font.width;

    return result;
}

char SSD1351::writeString(const char *str, SSD1351_FontDef font,
                          uint16_t color, uint16_t bgcolor)
{
    while (*str)
    {
        // Line-wrap
        if (_cursorX + font.width >= WIDTH)
        {
            _cursorX  = 0;
            _cursorY += font.height;
            if (_cursorY + font.height >= HEIGHT)
                return *str;

            if (*str == ' ')
            {
                ++str;
                continue;
            }
        }

        if (writeChar(*str, font, color, bgcolor) != *str)
            return *str;

        ++str;
    }

    return '\0';
}

// ---------------------------------------------------------------------------
// Hardware colour inversion
// ---------------------------------------------------------------------------

void SSD1351::invertColors(bool invert)
{
    select();
    writeCommand(invert ? 0xA7 /* INVERTDISPLAY */ : 0xA6 /* NORMALDISPLAY */);
    unselect();
}
