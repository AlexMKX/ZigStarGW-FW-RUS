void initWebServer();
void webServerHandleClient();
void handleGeneral();
void handleRoot();
void handleWifi();
void handleEther();
void handleSerial();
void handleMqtt();
void handleSaveSerial();
void handleNotFound();
void handleSaveWifi();
void handleSaveEther();
void handleSaveMqtt();
void handleLogs();
void handleReboot();
void handleUpdate();
void handleFSbrowser();
void handleReadfile();
void handleSavefile();
void handleLogBuffer();
void handleScanNetwork();
void handleClearConsole();
void handleGetVersion();
void handleZigRestart();
void handleZigbeeRestart();
void handleZigbeeBSL();
void handleSaveGeneral();
void handleHelp();
void handleESPUpdate();
void handleLoggedOut();
void printLogTime();
void printLogMsg(String msg);
void handleSaveSucces(String msg);
void handle_functions_js();
void handle_jquery_js();
void handle_bootstrap_js();
void handle_required_css();
void handle_toast_js();
void handle_glyphicons_woff();
void handle_logo_png();
void handle_wait_gif();
void handle_nok_png();
void handle_ok_png();
void zigbeeCmdSend(uint8_t one_byte);
bool checkAuth();
void handleWEBUpdate();
void checkUpdateFirmware();
void runUpdateFirmware(uint8_t *data, size_t len);


#define UPD_FILE "https://github.com/Zud71/ZigStarGW-FW/releases/latest/download/ZigStarGW.bin"
