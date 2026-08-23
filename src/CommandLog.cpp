#include "CommandLog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
LogEntry g_entries[CommandLog::CAPACITY];
size_t g_count = 0;
size_t g_next = 0;
SemaphoreHandle_t g_mutex = xSemaphoreCreateMutex();
}  // namespace

void CommandLog::add(const String &source, const String &status, const String &message) {
    LogEntry e;
    e.uptimeMs = millis();
    e.source = source;
    e.status = status;
    e.message = message;

    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_entries[g_next] = e;
        g_next = (g_next + 1) % CAPACITY;
        if (g_count < CAPACITY) g_count++;
        xSemaphoreGive(g_mutex);
    }
}

void CommandLog::collect(std::vector<LogEntry> &out) {
    out.clear();
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t n = g_count;
        size_t start = (g_count < CAPACITY) ? 0 : g_next;
        for (size_t i = 0; i < n; i++) out.push_back(g_entries[(start + i) % CAPACITY]);
        xSemaphoreGive(g_mutex);
    }
}
