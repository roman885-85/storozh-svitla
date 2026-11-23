#pragma once
#include <Arduino.h>
#include <esp_timer.h>

#define TZ_KYIV "EET-2EEST,M3.5.0/3,M10.5.0/4"

inline int64_t nowUs() { return esp_timer_get_time(); }

void   utilBegin();
bool   clockValid();
void   clockFromEpoch(time_t t, const char* src);  // запасне джерело часу
String fmtClock(int64_t us);        // «о 14:02» / «16.11 о 23:10» / «12 хв тому»
String fmtNowFull();                // «17.11.2026 14:05:33»
String fmtDur(int64_t sec);         // «3 год 25 хв»
String fmtHours(int64_t sec);       // «5 год 12 хв» — лише години й хвилини, і понад добу («26 год 4 хв»)
String fmtHM(int64_t us);           // «14:02» / «16.11 23:10» / «12 хв тому» — без «о», для «з 14:02»
String htmlEsc(const String& s);    // & < > — текст для Telegram у режимі HTML
String stripTags(const String& s);  // без <b>… і з &amp; &lt; &gt; назад — для журналу й запасного надсилання
String jsonEsc(const String& s);
String macToStr(const uint8_t m[6]);
bool   strToMac(const String& s, uint8_t m[6]);
String resetReasonText();
void   markBootAfterUpdate();      // 1.0.25: цей запуск — одразу після оновлення прошивки по мережі (нова версія на перевірці)
bool   bootAfterUpdate();
void   logEvent(const String& text);  // журнал подій — у RAM; 1.0.29: останні рядки ще й у RTC («чорна скринька», box.h)
void   logEventLocal(const String& text);   // лише в RAM — для рядків, що вже прийшли зі «скриньки»
String logJson(int maxN);
String logEntryJson(int i);         // 1.0.32: i-й запис від найновішого, «» — немає
String logMemJson();                // 1.0.38: де журнал (IRAM чи ні), місткість, зайнято, рядків
String logText(int maxN, size_t maxLen);   // «📋 Журнал» у боті (1.0.21): нові згори, HTML-безпечно
