// Opt-in, USB-fed ESP32-C6 ESP-Hosted firmware updater for JC8012P4A1C.
// This is a separate maintenance build, NEVER the normal Tactula firmware.
// The P4 app is flashed at 0x10000 only; touch calibration firmware in NVS
// and the P4 bootloader/partitions are not touched.
// C6 OTA has risk: loss of radio may require physical C6 UART recovery.
#include <Arduino.h>
#include <lvgl.h>
#include <lv_setup_JC8012P4A1C.hpp>
#include <WiFi.h>
#include <esp_hosted.h>
#include <esp_hosted_host_fw_ver.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if !defined(ESP_HOSTED_VERSION_MAJOR_1) || !defined(ESP_HOSTED_VERSION_MINOR_1) || !defined(ESP_HOSTED_VERSION_PATCH_1)
#error "Cannot confirm ESP-Hosted host library version: abort C6 updater build"
#endif
static_assert(ESP_HOSTED_VERSION_MAJOR_1 == 2 &&
              ESP_HOSTED_VERSION_MINOR_1 == 12 &&
              ESP_HOSTED_VERSION_PATCH_1 == 3,
              "Refusing C6 OTA: host ESP-Hosted must be exactly 2.12.3");

SET_LOOP_TASK_STACK_SIZE(24 * 1024);

namespace {
FILE* console = nullptr;

// USB CDC RX may buffer less than a 1-KiB hex packet. Keep each line
// comfortably below a 256-byte USB RX buffer and ACK every packet.
constexpr size_t kChunk = 96;
constexpr size_t kLineMax = 256;
// Batch fourteen 96-byte USB CDC packets into one 1344-byte hosted OTA RPC.
constexpr size_t kRpcChunk = kChunk * 14;
constexpr size_t kImageSize = 1206528;
constexpr char kImageHash[] =
    "12ea6b5e774863cd6c577b57879fff60a1580916db756f29e0d365d71f0cfa0b";
constexpr char kTargetVersion[] = "2.12.3";
constexpr char kConfirm[] = "FLASH-C6-2.12.3";
char line[kLineMax];
size_t lineLength = 0;
bool discardingLine = false;
uint8_t bytes[kChunk];
uint8_t rpcBytes[kRpcChunk];
size_t rpcLength = 0;
uint32_t rpcCount = 0;
bool otaStarted = false;
bool otaAttempted = false;
size_t expectedSize = 0;
size_t received = 0;
unsigned nextSequence = 0;
mbedtls_sha256_context sha;

void fail(const char* message) {
    fprintf(console, "ERR %s\n", message);
    // Don't pretend a partial OTA was successful. Leaving an incomplete
    // inactive partition is preferable to validating a truncated image.
}

void showVersion() {
    esp_hosted_coprocessor_fwver_t v = {};
    const int result = esp_hosted_get_coprocessor_fwversion(&v);
    if (result == ESP_OK) {
        fprintf(console, "VERSION C6=%lu.%lu.%lu HOST=2.12.3\n",
                      (unsigned long)v.major1, (unsigned long)v.minor1,
                      (unsigned long)v.patch1);
    } else {
        fprintf(console, "ERR VERSION_QUERY %d\n", result);
    }
}
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
uint32_t crc32(const uint8_t* data, size_t count) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < count; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return ~crc;
}
void handle(const char* command) {
    if (!strcmp(command, "VERSION")) {
        showVersion();
        return;
    }
    if (!strcmp(command, "REBOOT")) {
        if (otaStarted) { fail("OTA_IN_PROGRESS"); return; }
        fputs("REBOOTING P4\n", console);
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
        ESP.restart();
        return;
    }
    if (!strncmp(command, "BEGIN ", 6)) {
        if (otaAttempted) { fail("ALREADY_STARTED"); return; }
        char keyword[32] = {};
        char confirm[32] = {};
        unsigned long count = 0;
        char digest[65] = {};
        char extra = 0;
        if (sscanf(command, "BEGIN %31s %31s %lu %64s %c",
                   keyword, confirm, &count, digest, &extra) != 4 ||
            strcmp(keyword, kTargetVersion) ||
            strcmp(confirm, kConfirm) ||
            count != kImageSize || strcmp(digest, kImageHash)) {
            fail("INVALID_BEGIN"); return;
        }
        esp_hosted_coprocessor_fwver_t startingVersion = {};
        const int vErr = esp_hosted_get_coprocessor_fwversion(&startingVersion);
        if (vErr != ESP_OK || startingVersion.major1 != 2 ||
            startingVersion.minor1 != 3 || startingVersion.patch1 != 2) {
            fail("EXPECTED_C6_2_3_2"); return;
        }
        // Host framework is pinned to ESP-Hosted 2.12.3. Refuse to perform
        // OTA when runtime C6 version unexpectedly differs from the
        // inspected original firmware; this is not a generic C6 flasher.
        otaAttempted = true;
        const int result = esp_hosted_slave_ota_begin();
        fprintf(console, "OTA_BEGIN %s (%d)\n",
                      esp_err_to_name(static_cast<esp_err_t>(result)), result);
        if (result != ESP_OK) { fail("OTA_BEGIN_FAILED"); return; }
        expectedSize = count;
        received = 0;
        nextSequence = 0;
        rpcLength = 0;
        rpcCount = 0;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        otaStarted = true;
        fputs("READY\n", console);
        return;
    }
    if (!strncmp(command, "DATA ", 5)) {
        if (!otaStarted) { fail("NOT_STARTED"); return; }
        unsigned sequence = 0, length = 0;
        unsigned long expectedCrc = 0;
        char hex[2 * kChunk + 1] = {};
        char extra = 0;
        const int fields = sscanf(command, "DATA %u %u %lx %192s %c",
                                  &sequence, &length, &expectedCrc, hex, &extra);
        const size_t hexLength = strlen(hex);
        if (fields != 4 || sequence != nextSequence ||
            length == 0 || length > kChunk ||
            received + length > expectedSize || hexLength != length * 2) {
            fprintf(console, "ERR INVALID_CHUNK fields=%d seq=%u expected=%u len=%u hexlen=%lu received=%lu\n",
                          fields, sequence, nextSequence, length,
                          (unsigned long)hexLength, (unsigned long)received);
            return;
        }
        for (unsigned i = 0; i < length; ++i) {
            int hi = nibble(hex[2*i]), lo = nibble(hex[2*i+1]);
            if (hi < 0 || lo < 0) { fail("NON_HEX_CHUNK"); return; }
            bytes[i] = (hi << 4) | lo;
        }
        if (crc32(bytes, length) != static_cast<uint32_t>(expectedCrc)) {
            fail("CRC_MISMATCH"); return;
        }
        // Fail closed if the stream isn't an ESP-IDF application image.
        // Image header: magic 0xE9; ESP32-C6 chip ID 0x000D at bytes 12-13.
        if (received == 0 &&
            (length < 14 || bytes[0] != 0xE9 ||
             bytes[12] != 0x0D || bytes[13] != 0x00)) {
            fail("NOT_ESP32C6_APPLICATION"); return;
        }
        if (rpcLength + length > sizeof(rpcBytes)) {
            fail("RPC_BUFFER_OVERFLOW");
            mbedtls_sha256_free(&sha);
            otaStarted = false;
            return;
        }
        memcpy(rpcBytes + rpcLength, bytes, length);
        rpcLength += length;
        // Keep the USB serial transport small but issue fewer, larger C6
        // writes. The final short block is sent before accepting END.
        if (rpcLength == sizeof(rpcBytes) || received + length == expectedSize) {
            const size_t rpcOffset = received + length - rpcLength;
            fprintf(console, "[P4 C6 OTA RPC] START #%lu offset=%lu bytes=%lu\n",
                          (unsigned long)(rpcCount + 1), (unsigned long)rpcOffset,
                          (unsigned long)rpcLength);
            const uint32_t rpcAt = millis();
            const int result = esp_hosted_slave_ota_write(rpcBytes, rpcLength);
            fprintf(console, "[P4 C6 OTA RPC] END #%lu result=%d elapsed=%lu ms\n",
                          (unsigned long)(rpcCount + 1), result,
                          (unsigned long)(millis() - rpcAt));
            if (result != ESP_OK) {
                fprintf(console, "ERR OTA_WRITE %d AT=%lu\n", result,
                              (unsigned long)rpcOffset);
                mbedtls_sha256_free(&sha);
                otaStarted = false;
                return;
            }
            ++rpcCount;
            rpcLength = 0;
        }
        mbedtls_sha256_update(&sha, bytes, length);
        received += length;
        ++nextSequence;
        fprintf(console, "ACK %u %lu\n", sequence, (unsigned long)received);
        return;
    }
    if (!strcmp(command, "END")) {
        if (!otaStarted || received != expectedSize || rpcLength != 0) {
            fail("INCOMPLETE_IMAGE_OR_PENDING_RPC"); return;
        }
        uint8_t digest[32];
        mbedtls_sha256_finish(&sha, digest);
        mbedtls_sha256_free(&sha);
        char calculated[65] = {};
        for (size_t i = 0; i < 32; ++i)
            snprintf(calculated + i*2, sizeof(calculated) - i*2, "%02x", digest[i]);
        if (strcmp(calculated, kImageHash)) {
            otaStarted = false;
            fail("FULL_SHA256_MISMATCH_DO_NOT_ACTIVATE");
            return;
        }
        // Current C6 is v2.3.2. The official ESP-Hosted 2.12.3 example
        // documents no separate OTA_ACTIVATE on pre-2.6 slaves.
        const int result = esp_hosted_slave_ota_end();
        fprintf(console, "OTA_END %s (%d)\n",
                      esp_err_to_name(static_cast<esp_err_t>(result)), result);
        otaStarted = false;
        if (result == ESP_OK)
            fputs("SUCCESS C6 OTA validated. Restart P4 to query installed version.\n", console);
        else
            fail("OTA_END_FAILED");
        return;
    }
    if (!strcmp(command, "HELP")) {
        fputs("Commands: VERSION; REBOOT; BEGIN 2.12.3 FLASH-C6-2.12.3 <size> <sha256>; DATA <seq> <len> <crc32hex> <hex>; END\n", console);
        return;
    }
    fail("UNKNOWN_COMMAND");
}
} // namespace

void setup() {
    // Use the same USB port as before, but leave FIFO ownership to ESP-IDF.
    // Route its console VFS through the driver before starting hosted Wi-Fi.
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    config.rx_buffer_size = 512;
    config.tx_buffer_size = 1024;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    ESP_ERROR_CHECK(usb_serial_jtag_vfs_register());
#endif
    console = fopen("/dev/usbserjtag", "w");
    ESP_ERROR_CHECK(console ? ESP_OK : ESP_FAIL);
    setvbuf(console, nullptr, _IOLBF, BUFSIZ);
    // This board's native USB CDC port may re-enumerate on reset. Preserve
    // a generous window for the Windows serial monitor to reconnect.
    delay(8000);
    fputs("\n[P4 C6 OTA STAGE 01] maintenance boot; no C6 flash will start automatically\n", console);
    fputs("[P4 C6 OTA STAGE 02] before board.begin\n", console);
    board.begin();
    fputs("[P4 C6 OTA STAGE 03] board.begin returned\n", console);
    board.setRadioPower(true);
    fputs("[P4 C6 OTA STAGE 04] C6 power enabled\n", console);
    delay(200);
    fputs("[P4 C6 OTA STAGE 05] before WiFi.mode(WIFI_STA)\n", console);
    const bool modeOk = WiFi.mode(WIFI_STA);
    fprintf(console, "[P4 C6 OTA STAGE 06] hosted WiFi.mode returned=%d (not connecting)\n", int(modeOk));
    fputs("[P4 C6 OTA STAGE 07] before C6 firmware version query\n", console);
    showVersion();
    fputs("[P4 C6 OTA STAGE 08] version query returned\n", console);
    fputs("READY_FOR_COMMAND\n", console);
}
void loop() {
    static uint32_t lastHeartbeat = 0;
    if ((uint32_t)(millis() - lastHeartbeat) >= 5000) {
        lastHeartbeat = millis();
        fprintf(console, "[P4 C6 OTA] heartbeat uptime=%lu, ota=%u, received=%lu\n",
                      (unsigned long)lastHeartbeat, unsigned(otaStarted),
                      (unsigned long)received);
    }
    char c;
    while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) {
        if (c == '\r') continue;
        if (c == '\n') {
            line[lineLength] = 0;
            if (!discardingLine) handle(line);
            lineLength = 0;
            discardingLine = false;
        } else if (discardingLine) {
            continue;
        } else if (lineLength < kLineMax - 1) {
            line[lineLength++] = c;
        } else {
            // Do not reinterpret the remainder of an overlong packet as
            // another command. Drop through newline; safely reject chunk.
            discardingLine = true;
            fail("LINE_TOO_LONG");
        }
    }
    delay(1);
}
