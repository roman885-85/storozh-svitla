#include "crash.h"
#include "util.h"
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_system.h>

//  Функції ядра дамп читають через відображення флешу в пам'ять (mmap), кадри стека в них — 48–144 Б: можна кликати
//  з обробника сторінки (запас стека основної задачі ~4 КБ).

static String hex32(uint32_t v) {
  char b[12];
  snprintf(b, sizeof b, "0x%08x", (unsigned)v);
  return b;
}

size_t crashSize() {
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK) return 0;
  return size;
}

bool crashRead(size_t off, uint8_t* buf, size_t n) {
  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK || off + n > size) return false;
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  if (!p || addr < p->address) return false;
  return esp_partition_read(p, addr - p->address + off, buf, n) == ESP_OK;
}

//  Підсумок дампу: причина (рядок обробника збою), задача, що виконувалась, і адреси її стека. maxBt — скільки адрес.
struct Sum { bool ok = false; String reason, task, pc, bt, elf; bool btBad = false; uint32_t cause = 0, vaddr = 0; };
static Sum summary(int maxBt, const char* sep) {
  Sum r;
  esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
  if (!s) return r;
  if (esp_core_dump_get_summary(s) == ESP_OK) {
    r.ok = true;
    s->exc_task[sizeof s->exc_task - 1] = 0;
    r.task = s->exc_task;
    r.pc = hex32(s->exc_pc);
    int n = (int)s->exc_bt_info.depth;
    if (n > 16) n = 16;
    if (n > maxBt) n = maxBt;
    for (int i = 0; i < n; i++) r.bt += String(i ? sep : "") + hex32(s->exc_bt_info.bt[i]);
    r.btBad = s->exc_bt_info.corrupted;
    r.cause = s->ex_info.exc_cause;
    r.vaddr = s->ex_info.exc_vaddr;
    s->app_elf_sha256[sizeof s->app_elf_sha256 - 1] = 0;
    r.elf = (const char*)s->app_elf_sha256;
  }
  free(s);
  char why[160];
  if (esp_core_dump_get_panic_reason(why, sizeof why) == ESP_OK) { why[sizeof why - 1] = 0; r.reason = why; r.reason.trim(); }
  return r;
}

void crashBoot() {
  esp_reset_reason_t why = esp_reset_reason();
  if (why != ESP_RST_PANIC && why != ESP_RST_INT_WDT && why != ESP_RST_TASK_WDT && why != ESP_RST_WDT) return;
  if (esp_core_dump_image_check() != ESP_OK) {
    logEvent("Дампу збою у флеші немає (чи він зіпсований) — де стояла програма, не видно");
    return;
  }
  Sum s = summary(8, " ");
  if (!s.ok) { logEvent("Дамп збою у флеші є, але прочитати його підсумок не вдалось (" + String((unsigned)crashSize()) + " Б)"); return; }
  logEvent("Дамп збою (" + String((unsigned)(crashSize() / 1024)) + " КБ, для розбору): " + (s.reason.length() ? s.reason + " · " : String()) +
           "задача " + s.task + " · " + s.bt + (s.btBad ? " (стек зіпсований)" : ""));
}

String crashJson() {
  size_t n = crashSize();
  if (!n) return "{\"has\":false}";
  bool valid = esp_core_dump_image_check() == ESP_OK;
  String o = "{\"has\":true,\"size\":" + String((unsigned)n) + ",\"valid\":" + (valid ? "true" : "false");
  if (valid) {
    Sum s = summary(16, "\",\"");
    if (s.ok) {
      o += ",\"reason\":\"" + jsonEsc(s.reason) + "\",\"task\":\"" + jsonEsc(s.task) + "\",\"pc\":\"" + s.pc + "\",\"bt\":[" +
           (s.bt.length() ? "\"" + s.bt + "\"" : String()) + "],\"btBad\":" + (s.btBad ? "true" : "false") +
           ",\"cause\":" + String((unsigned)s.cause) + ",\"vaddr\":\"" + hex32(s.vaddr) + "\",\"elf\":\"" + jsonEsc(s.elf) + "\"";
    }
  }
  return o + ",\"resetNow\":" + String((int)esp_reset_reason()) + "}";
}
