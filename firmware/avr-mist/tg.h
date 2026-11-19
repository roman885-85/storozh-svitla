#pragma once
#include <Arduino.h>

/*  Telegram. Окрема задача робить лише мережу: шле чергу й раз на 10 с забирає
    вхідні повідомлення у «вхідні». Команди (/status, /start) розбирає tgLoop()
    в основному циклі — там живуть налаштування й стан, без гонок між задачами.
    Черга й журнал — лише в RAM.  */
void    tgBegin();
void    tgLoop();                                      // з loop()
void    tgReload();                                    // після збереження налаштувань
//  cat (1.0.21) — що це за повідомлення: біти RF_* з settings.h; отримають ті, у кого ввімкнено хоч один із них.
//  0 — усім, у кого сповіщення ввімкнено; C_ADMIN — ще й адміністраторам (навіть із вимкненими сповіщеннями).
constexpr uint16_t C_ADMIN = 0x8000;
bool    tgBroadcast(int64_t evUs, const String& text, bool silent, uint16_t cat); // %T — час події; HTML Telegram; silent — без звуку
void    tgReply(long long chat, const String& text, uint8_t kb = 0);  // kb: 1 — показати кнопку «📊 Стан», 2 — прибрати
void    tgWelcome(long long chat);                     // «Вас додано до сповіщень»
void    tgFarewell(long long chat);                    // «Сповіщення для вас вимкнено»
String  tgJson();
//  Картинка профілю бота (1.0.9). Тіло multipart збирає сторінка моста (web.cpp), задача Telegram надсилає його
//  в setMyProfilePhoto і звільняє. Стан — у tgJson: "photo".
#define TG_PHOTO_BOUNDARY "----avrmistPhoto7948"
bool    tgSetPhoto(uint8_t* body, size_t len);          // забирає body (malloc); false — попередня ще надсилається
bool    tgSetName(const String& name);                  // нова назва бота; false — попередня зміна ще триває
int64_t tgHeartbeatUs();
bool    tgStuck();
int     tgHealth();
//  Оголошення (1.0.26): усім, у кого ввімкнено сповіщення (nIds < 0), або вибраним з них; повертає, скільком поставлено в чергу
int     tgAnnounce(const String& text, const String& by, const long long* ids, int nIds, bool silent);                                    // 1.0.22, для дисплея: 1 — працює, 0 — збій, -1 — не налаштовано                                     // 1.0.18: Telegram працював, а тепер 10 хв мовчить — час перезапуститись (черга в RTC)
