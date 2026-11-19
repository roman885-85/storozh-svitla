/*  Ядро Arduino 3.3.3 (NetworkManager::hostByName) на першому запиті після появи чи
    втрати адреси чистить кеш DNS (dns_clear_cache) без замка lwIP — і той, хто чекав
    відповіді DNS (SNTP, Telegram), лізе в lwIP без замка: assert «Required to lock TCPIP
    core functionality!». Знайдено на ПОТУЖНОМУ РАДІО 26.10.2026. Компонувальник
    (tools/build.sh: -Wl,--wrap=dns_clear_cache) веде виклик сюди — чистимо під замком.  */
#include "lwip/tcpip.h"

extern "C" void __real_dns_clear_cache(void);
extern "C" void __wrap_dns_clear_cache(void) {
  if (sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER)) { __real_dns_clear_cache(); return; }
  LOCK_TCPIP_CORE();
  __real_dns_clear_cache();
  UNLOCK_TCPIP_CORE();
}
