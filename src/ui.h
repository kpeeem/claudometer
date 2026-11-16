#pragma once

#include <Arduino.h>

enum class Page : uint8_t { Overview, ClaudeDetail };

void uiInit();
void uiApplyTheme();
const char *uiThemeName(uint8_t which);
void uiHelpScreen();
void uiRenderFull(Page page);
void uiRenderValues(Page page);
void uiMessage(const char *title, const char *line1, const char *line2);
void uiPortalScreen(const char *ssid, const char *ip);
void uiSetBusy(bool busy);
bool uiSupportsPartial();
void uiHibernate();
void uiRenderPlanes(Page page);
const uint8_t *uiPlane(bool red);
size_t uiPlaneBytes();

#if UI_MOCK
void uiDumpPage(Page page);
void uiDumpPortal();
void uiDumpHelp();
#endif
