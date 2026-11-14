#ifndef WEB_SERVER_H
#define WEB_SERVER_H

void initWebServer();
void handleWebServer();
// True while the local manual firmware upload is using the OTA partition.
bool webOtaBusy();

#endif // WEB_SERVER_H
