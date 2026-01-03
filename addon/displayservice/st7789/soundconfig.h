#ifndef _ST7789_SOUNDCONFIG_H
#define _ST7789_SOUNDCONFIG_H

#include <circle/2dgraphics.h>
#include <circle/spimaster.h>
#include <configservice/configservice.h>
#include <display/st7789display.h>
#include <displayservice/buttons.h>
#include <displayservice/ipage.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

class ST7789SoundConfigPage : public IPage {
   public:
    ST7789SoundConfigPage(CST7789Display* display, C2DGraphics* graphics);
    ~ST7789SoundConfigPage();
    void OnEnter() override;
    void OnExit() override;
    void OnButtonPress(Button buttonId) override;
    void Refresh() override;
    virtual bool shouldChangePage() override;
    virtual const char* nextPageName() override;

   private:
    void Draw();
    void MoveSelection(int delta);
    void SaveAndReboot();
    void DrawConfirmation(const char* message);
    void DrawNavigationBar(const char* screenType);

   private:
    bool m_ShouldChangePage = false;
    CST7789Display* m_Display;
    C2DGraphics* m_Graphics;
    ConfigService* config;
    const char* options[4] = {
        "I2S Audio (HATs)",
        "PWM Audio (3.5mm)",
        "HDMI Audio",
        "Disabled"
    };
    size_t m_SelectedIndex = 0;
};
#endif
