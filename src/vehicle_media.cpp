#include "vehicle_media.h"

#include <WebServer.h>
#include <WiFi.h>

namespace VehicleMedia {
namespace {
constexpr int kSdClk = 38, kSdCmd = 40, kSdD0 = 39, kSdD1 = 41, kSdD2 = 48, kSdD3 = 47;
constexpr size_t kMaxUploadBytes = 10UL * 1024UL * 1024UL;
constexpr char kVehicleTemporary[] = "/vehicle_upload.tmp";
constexpr char kVehicleBackup[] = "/vehicle_upload.bak";
constexpr char kBootTemporary[] = "/boot_logo_upload.tmp";
constexpr char kBootBackup[] = "/boot_logo_upload.bak";
constexpr char kBoot2Temporary[] = "/boot_logo2_upload.tmp";
constexpr char kBoot2Backup[] = "/boot_logo2_upload.bak";

WebServer server(80);
File uploadFile;
String uploadTarget, uploadTemporary, uploadBackup, uploadResult = "Insira um cartao microSD formatado em FAT32.", savedMediaPath;
// Os estados das logos são atualizados somente ao iniciar, depois de um envio ou
// após reiniciar. Assim a aba Personalizar não acessa o cartão microSD enquanto desenha.
String savedBootLogoPath, savedBootLogo2Path;
bool serverActive = false, sdReady = false, uploadValid = false, restartPending = false;
// 0=foto do veiculo, 1=EXTRA1, 2=EXTRA2.
uint8_t uploadingBootLogo = 0;
size_t uploadBytes = 0;
unsigned long restartAt = 0;

void refreshSavedMediaPath() {
  savedMediaPath = "";
  savedBootLogoPath = "";
  savedBootLogo2Path = "";
  if (!sdReady) return;
  if (SD_MMC.exists("/vehicle.png")) savedMediaPath = "/vehicle.png";
  else if (SD_MMC.exists("/vehicle.jpg")) savedMediaPath = "/vehicle.jpg";
  else if (SD_MMC.exists("/vehicle.jpeg")) savedMediaPath = "/vehicle.jpeg";
  if (SD_MMC.exists("/boot_logo.png")) savedBootLogoPath = "/boot_logo.png";
  else if (SD_MMC.exists("/boot_logo.jpg")) savedBootLogoPath = "/boot_logo.jpg";
  else if (SD_MMC.exists("/boot_logo.jpeg")) savedBootLogoPath = "/boot_logo.jpeg";
  if (SD_MMC.exists("/boot_logo2.png")) savedBootLogo2Path = "/boot_logo2.png";
  else if (SD_MMC.exists("/boot_logo2.jpg")) savedBootLogo2Path = "/boot_logo2.jpg";
  else if (SD_MMC.exists("/boot_logo2.jpeg")) savedBootLogo2Path = "/boot_logo2.jpeg";
}

struct StaticCache {
  lgfx::LGFX_Sprite sprite;
  bool ready = false;
  String path;
  int width = 0, height = 0, maxWidth = 0, maxHeight = 0;
};
StaticCache staticCaches[3];

bool readStaticDimensions(const char *path, int &width, int &height) {
  width = height = 0;
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) return false;
  uint8_t h[24] = {};
  const size_t n = file.read(h, sizeof(h));
  if (n >= 24 && !memcmp(h, "\x89PNG\r\n\x1a\n", 8)) {
    width = (h[16] << 24) | (h[17] << 16) | (h[18] << 8) | h[19];
    height = (h[20] << 24) | (h[21] << 16) | (h[22] << 8) | h[23];
    file.close(); return width > 0 && height > 0;
  }
  file.seek(0); uint8_t marker[2];
  if (file.read(marker, 2) != 2 || marker[0] != 0xFF || marker[1] != 0xD8) { file.close(); return false; }
  while (file.available()) {
    do { if (file.read(&marker[0], 1) != 1) { file.close(); return false; } } while (marker[0] != 0xFF);
    do { if (file.read(&marker[1], 1) != 1) { file.close(); return false; } } while (marker[1] == 0xFF);
    uint8_t len[2]; if (file.read(len, 2) != 2) break;
    const uint16_t length = (len[0] << 8) | len[1];
    const bool sof = (marker[1] >= 0xC0 && marker[1] <= 0xC3) || (marker[1] >= 0xC5 && marker[1] <= 0xC7) || (marker[1] >= 0xC9 && marker[1] <= 0xCB) || (marker[1] >= 0xCD && marker[1] <= 0xCF);
    if (sof && length >= 7) {
      uint8_t size[5];
      if (file.read(size, 5) == 5) { height = (size[1] << 8) | size[2]; width = (size[3] << 8) | size[4]; }
      file.close(); return width > 0 && height > 0;
    }
    file.seek(file.position() + length - 2);
  }
  file.close(); return false;
}
void invalidateStaticCache() { for (auto &cache : staticCaches) { cache.ready = false; cache.path = ""; cache.sprite.deleteSprite(); } }
StaticCache *findCache(const char *path, int width, int height) { for (auto &cache : staticCaches) if (cache.ready && cache.path == path && cache.maxWidth == width && cache.maxHeight == height) return &cache; return nullptr; }
StaticCache *freeCache() { for (auto &cache : staticCaches) if (!cache.ready) return &cache; return &staticCaches[0]; }

const char kPhotoPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>HONDAPP FOTO</title><style>body{margin:0;background:#080808;color:#eee;font:16px Arial;padding:22px}h1{color:#e40521;font-size:23px}.card{border:1px solid #e40521;border-radius:10px;padding:18px;max-width:440px}input,button{width:100%;box-sizing:border-box;margin-top:14px;padding:13px;border-radius:6px}button{background:#e40521;color:#fff;border:0;font-weight:bold}small{color:#aaa}</style></head><body><div class="card"><h1>HONDAPP // FOTO</h1><p>Envie uma foto JPEG ou PNG para o cartao microSD.</p><form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" name="media" accept="image/jpeg,image/png" required><button type="submit">ENVIAR E SUBSTITUIR</button></form><p><small>JPG ou PNG, ate 10 MB. O novo arquivo so substitui o anterior depois de salvo por completo.</small></p></div></body></html>)HTML";
const char kBootPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>HONDAPP LOGO</title><style>body{margin:0;background:#080808;color:#eee;font:16px Arial;padding:22px}h1{color:#e40521;font-size:23px}.card{border:1px solid #e40521;border-radius:10px;padding:18px;max-width:440px}input,button{width:100%;box-sizing:border-box;margin-top:14px;padding:13px;border-radius:6px}button{background:#e40521;color:#fff;border:0;font-weight:bold}small{color:#aaa}</style></head><body><div class="card"><h1>HONDAPP // LOGO DE INICIALIZACAO</h1><p>Envie uma imagem JPEG ou PNG para usar como logo EXTRA no cartao microSD.</p><form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" name="media" accept="image/jpeg,image/png" required><button type="submit">ENVIAR LOGO</button></form><p><small>JPG ou PNG, ate 10 MB. Sem cartao ou sem arquivo salvo, o painel inicia automaticamente com a logo Honda padrao.</small></p></div></body></html>)HTML";

String extensionFor(String name) { name.toLowerCase(); if (name.endsWith(".jpg")) return ".jpg"; if (name.endsWith(".jpeg")) return ".jpeg"; if (name.endsWith(".png")) return ".png"; return String(); }
void sendPhotoPage() { server.send_P(200, "text/html", kPhotoPage); }
void sendBootPage() { server.send_P(200, "text/html", kBootPage); }
void removeAllMedia() { SD_MMC.remove("/vehicle.jpg"); SD_MMC.remove("/vehicle.jpeg"); SD_MMC.remove("/vehicle.png"); SD_MMC.remove("/vehicle.gif"); }

void completeUpload() {
  bool ok = false;
  if (uploadValid && SD_MMC.exists(uploadTemporary.c_str()) && uploadBytes && uploadBytes <= kMaxUploadBytes) {
    SD_MMC.remove(uploadBackup.c_str());
    const bool hadOld = SD_MMC.exists(uploadTarget.c_str());
    const bool protectedOld = !hadOld || SD_MMC.rename(uploadTarget.c_str(), uploadBackup.c_str());
    if (protectedOld && SD_MMC.rename(uploadTemporary.c_str(), uploadTarget.c_str())) {
      File file = SD_MMC.open(uploadTarget.c_str(), FILE_READ);
      ok = file && file.size() == uploadBytes;
      if (file) file.close();
    }
    if (ok) SD_MMC.remove(uploadBackup.c_str());
    else if (protectedOld && SD_MMC.exists(uploadBackup.c_str())) SD_MMC.rename(uploadBackup.c_str(), uploadTarget.c_str());
  }
  if (ok) {
    invalidateStaticCache();
    if (uploadingBootLogo) {
      const char *base = uploadingBootLogo == 1 ? "/boot_logo" : "/boot_logo2";
      const String jpg = String(base) + ".jpg";
      const String jpeg = String(base) + ".jpeg";
      const String png = String(base) + ".png";
      if (uploadTarget != jpg) SD_MMC.remove(jpg.c_str());
      if (uploadTarget != jpeg) SD_MMC.remove(jpeg.c_str());
      if (uploadTarget != png) SD_MMC.remove(png.c_str());
      // Atualiza o estado em RAM somente após o arquivo novo estar completo.
      if (uploadingBootLogo == 1) savedBootLogoPath = uploadTarget;
      else savedBootLogo2Path = uploadTarget;
    } else {
      if (uploadTarget != "/vehicle.jpg") SD_MMC.remove("/vehicle.jpg");
      if (uploadTarget != "/vehicle.jpeg") SD_MMC.remove("/vehicle.jpeg");
      if (uploadTarget != "/vehicle.png") SD_MMC.remove("/vehicle.png");
      SD_MMC.remove("/vehicle.gif"); refreshSavedMediaPath();
    }
    server.send(200, "text/html", "<body style='background:#080808;color:white;font-family:Arial;padding:25px'><h2>HONDAPP</h2><p>Imagem salva no cartao microSD. Reiniciando o painel...</p></body>");
    restartPending = true; restartAt = millis() + 800; return;
  }
  SD_MMC.remove(uploadTemporary.c_str());
  server.send(400, "text/plain", "Envio nao concluido. Use JPG ou PNG e tente novamente.");
}
void receiveUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadBytes = 0; uploadValid = false;
    const String extension = extensionFor(upload.filename);
    if (!extension.length()) return;
    SD_MMC.remove(uploadTemporary.c_str());
    uploadFile = SD_MMC.open(uploadTemporary.c_str(), FILE_WRITE);
    const char *base = uploadingBootLogo == 1 ? "/boot_logo" : (uploadingBootLogo == 2 ? "/boot_logo2" : "/vehicle");
    uploadTarget = String(base) + extension;
    uploadValid = (bool)uploadFile;
  } else if (upload.status == UPLOAD_FILE_WRITE && uploadValid) {
    if (uploadBytes + upload.currentSize > kMaxUploadBytes) { uploadValid = false; uploadFile.close(); SD_MMC.remove(uploadTemporary.c_str()); return; }
    uploadFile.write(upload.buf, upload.currentSize); uploadBytes += upload.currentSize;
  } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close(); if (upload.status == UPLOAD_FILE_ABORTED) uploadValid = false;
  }
}

bool drawPath(lgfx::LGFX_Sprite *target, const char *path, int x, int y, int maxWidth, int maxHeight) {
  if (!sdReady || !SD_MMC.exists(path)) return false;
  if (!findCache(path, maxWidth, maxHeight)) {
    int sourceWidth, sourceHeight;
    if (!readStaticDimensions(path, sourceWidth, sourceHeight)) return false;
    const float scale = min((float)maxWidth / sourceWidth, (float)maxHeight / sourceHeight);
    const int width = max(1, (int)(sourceWidth * scale + .5f)), height = max(1, (int)(sourceHeight * scale + .5f));
    StaticCache *cache = freeCache(); cache->sprite.deleteSprite(); cache->ready = false;
    cache->sprite.setColorDepth(16); cache->sprite.setPsram(true);
    if (!cache->sprite.createSprite(width, height)) return false;
    cache->sprite.fillScreen(0);
    const bool decoded = String(path).endsWith(".png") ? cache->sprite.drawPngFile(SD_MMC, path, 0, 0, width, height, 0, 0, scale) : cache->sprite.drawJpgFile(SD_MMC, path, 0, 0, width, height, 0, 0, scale);
    if (!decoded) { cache->sprite.deleteSprite(); return false; }
    cache->path = path; cache->width = width; cache->height = height; cache->maxWidth = maxWidth; cache->maxHeight = maxHeight; cache->ready = true;
  }
  StaticCache *cache = findCache(path, maxWidth, maxHeight);
  if (!cache) return false;
  if (target) cache->sprite.pushSprite(target, x + (maxWidth - cache->width) / 2, y + (maxHeight - cache->height) / 2); return true;
}
const char *bootLogoPath() { return savedBootLogoPath.length() ? savedBootLogoPath.c_str() : nullptr; }
const char *bootLogo2Path() { return savedBootLogo2Path.length() ? savedBootLogo2Path.c_str() : nullptr; }
} // namespace

bool begin() {
  if (!SD_MMC.setPins(kSdClk, kSdCmd, kSdD0, kSdD1, kSdD2, kSdD3)) return false;
  sdReady = SD_MMC.begin("/sdcard", false, false, 5);
  if (!sdReady) { uploadResult = "Cartao microSD nao encontrado. Insira um cartao FAT32 no slot integrado."; return false; }
  SD_MMC.remove(kVehicleTemporary); SD_MMC.remove(kVehicleBackup); SD_MMC.remove(kBootTemporary); SD_MMC.remove(kBootBackup); SD_MMC.remove(kBoot2Temporary); SD_MMC.remove(kBoot2Backup); SD_MMC.remove("/vehicle.gif");
  refreshSavedMediaPath(); uploadResult = "Cartao microSD pronto para receber JPG ou PNG."; return true;
}
bool exists() { return sdReady && savedMediaPath.length(); }
const char *fileName() { return savedMediaPath.c_str(); }
bool remove() { if (!sdReady) return false; invalidateStaticCache(); removeAllMedia(); refreshSavedMediaPath(); return !exists(); }
bool startUploadNetwork() { if (serverActive) return uploadingBootLogo == 0; if (!sdReady || WiFi.status() != WL_CONNECTED) return false; uploadingBootLogo = 0; uploadTemporary = kVehicleTemporary; uploadBackup = kVehicleBackup; server.on("/", HTTP_GET, sendPhotoPage); server.on("/upload", HTTP_POST, completeUpload, receiveUpload); server.begin(); serverActive = true; return true; }
bool bootLogoExists() { return savedBootLogoPath.length() != 0; }
bool bootLogo2Exists() { return savedBootLogo2Path.length() != 0; }
bool startBootLogoUploadNetwork() { if (serverActive) return uploadingBootLogo == 1; if (!sdReady || WiFi.status() != WL_CONNECTED) return false; uploadingBootLogo = 1; uploadTemporary = kBootTemporary; uploadBackup = kBootBackup; server.on("/", HTTP_GET, sendBootPage); server.on("/upload", HTTP_POST, completeUpload, receiveUpload); server.begin(); serverActive = true; return true; }
bool startBootLogo2UploadNetwork() { if (serverActive) return uploadingBootLogo == 2; if (!sdReady || WiFi.status() != WL_CONNECTED) return false; uploadingBootLogo = 2; uploadTemporary = kBoot2Temporary; uploadBackup = kBoot2Backup; server.on("/", HTTP_GET, sendBootPage); server.on("/upload", HTTP_POST, completeUpload, receiveUpload); server.begin(); serverActive = true; return true; }
bool preloadBootLogo(int maxWidth, int maxHeight) { const char *path = bootLogoPath(); return path && drawPath(nullptr, path, 0, 0, maxWidth, maxHeight); }
bool preloadBootLogo2(int maxWidth, int maxHeight) { const char *path = bootLogo2Path(); return path && drawPath(nullptr, path, 0, 0, maxWidth, maxHeight); }
bool drawBootLogo(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight) { const char *path = bootLogoPath(); return path && drawPath(&target, path, x, y, maxWidth, maxHeight); }
bool drawBootLogo2(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight) { const char *path = bootLogo2Path(); return path && drawPath(&target, path, x, y, maxWidth, maxHeight); }
void stopUploadNetwork() { if (serverActive) { server.stop(); serverActive = false; } }
bool uploadNetworkActive() { return serverActive; }
void handleNetwork() { if (serverActive) server.handleClient(); if (restartPending && millis() >= restartAt) { restartPending = false; ESP.restart(); } }
String uploadAddress() { return serverActive && WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(); }
String status() { return uploadResult; }
void tick() {}
bool preloadStatic(int maxWidth, int maxHeight) { return exists() && drawPath(nullptr, fileName(), 0, 0, maxWidth, maxHeight); }
bool draw(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight) { return exists() && drawPath(&target, fileName(), x, y, maxWidth, maxHeight); }
} // namespace VehicleMedia
