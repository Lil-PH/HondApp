#include "vehicle_media.h"

#include <WebServer.h>
#include <WiFi.h>
#include <AnimatedGIF.h>

namespace VehicleMedia {
namespace {
constexpr int kSdClk = 38, kSdCmd = 40, kSdD0 = 39, kSdD1 = 41, kSdD2 = 48, kSdD3 = 47;
constexpr size_t kMaxUploadBytes = 10UL * 1024UL * 1024UL;
constexpr char kVehicleTemporary[] = "/vehicle_upload.tmp";
constexpr char kVehicleBackup[] = "/vehicle_upload.bak";
constexpr char kGifTestTemporary[] = "/vehicle_gif_test.tmp";
constexpr char kGifTestBackup[] = "/vehicle_gif_test.bak";
// O GIF enviado pelo formulário principal é a mídia real do Vehicle Profile.
// Mantemos este identificador interno para não alterar a interface do painel.
constexpr char kGifTestPath[] = "/vehicle.gif";
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
// 0=foto do veiculo, 1=EXTRA1, 2=EXTRA2, 3=teste GIF do Vehicle Profile.
uint8_t uploadingBootLogo = 0;

AnimatedGIF gif;
// O GIF é composto em um sprite próprio antes de ir para o LCD. Assim, quando
// o HUD redesenha o relógio, o quadro já composto volta junto e transparências
// ou quadros parciais nunca deixam faixas pretas no Vehicle Profile.
lgfx::LGFX_Device *gifOutputTarget = nullptr;
LGFX_Sprite gifFrame;
int gifTargetX = 0, gifTargetY = 0, gifTargetWidth = 0, gifTargetHeight = 0;
uint16_t gifLine[296];
bool gifOpen = false;
// No máximo 20 atualizações por segundo; o decoder ainda respeita a duração
// individual gravada em cada quadro.
unsigned long gifNextFrameAt = 0;

void *gifOpenFile(const char *name, int32_t *size) {
  File *file = new File(SD_MMC.open(name, FILE_READ));
  if (!*file) { delete file; return nullptr; }
  *size = file->size();
  return file;
}
void gifCloseFile(void *handle) { File *file = static_cast<File *>(handle); if (file) { file->close(); delete file; } }
int32_t gifReadFile(GIFFILE *file, uint8_t *buffer, int32_t length) {
  File *source = static_cast<File *>(file->fHandle);
  if (!source) return 0;
  // Mantém um byte antes do fim: alguns cartões falham ao buscar logo após uma
  // leitura que termina exatamente no EOF, e AnimatedGIF precisa fazer seeks.
  if ((file->iSize - file->iPos) < length) length = file->iSize - file->iPos - 1;
  if (length <= 0) return 0;
  const int32_t read = source->read(buffer, length);
  file->iPos = source->position();
  return read;
}
int32_t gifSeekFile(GIFFILE *file, int32_t position) {
  File *source = static_cast<File *>(file->fHandle);
  if (!source || !source->seek(position)) return -1;
  file->iPos = source->position();
  return file->iPos;
}
void gifDraw(GIFDRAW *draw) {
  if (gifTargetWidth <= 0 || gifTargetHeight <= 0) return;
  const int canvasWidth = draw->iCanvasWidth;
  const int canvasHeight = gif.getCanvasHeight();
  if (canvasWidth <= 0 || canvasHeight <= 0) return;

  // RAW entrega uma linha do retângulo alterado. iX/iY são a origem desse
  // retângulo; usá-las é indispensável para GIFs com quadros parciais.
  const int sourceY = draw->iY + draw->y;
  if (sourceY < 0 || sourceY >= canvasHeight) return;
  const int outY = (sourceY * gifTargetHeight + canvasHeight - 1) / canvasHeight;
  if (outY < 0 || outY >= gifTargetHeight ||
      ((outY * canvasHeight) / gifTargetHeight) != sourceY) return;

  const uint8_t *pixels = draw->pPixels;
  const uint16_t *palette = draw->pPalette;
  gifFrame.startWrite();
  int runStart = -1;
  int runLength = 0;
  for (int outX = 0; outX <= gifTargetWidth; ++outX) {
    bool opaque = false;
    uint16_t color = 0;
    if (outX < gifTargetWidth) {
      const int sourceX = (outX * canvasWidth) / gifTargetWidth;
      if (sourceX >= draw->iX && sourceX < draw->iX + draw->iWidth) {
        const uint8_t index = pixels[sourceX - draw->iX];
        if (!draw->ucHasTransparency || index != draw->ucTransparent) {
          opaque = true;
          color = palette[index];
        }
      }
    }
    if (opaque) {
      if (runStart < 0) runStart = outX;
      gifLine[runLength++] = color;
    } else if (runStart >= 0) {
      gifFrame.pushImage(runStart, outY, runLength, 1, gifLine);
      runStart = -1;
      runLength = 0;
    }
  }
  gifFrame.endWrite();
  if ((draw->y & 0x0F) == 0) yield();
}
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

const char kGifTestPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>HONDAPP TESTE GIF</title><style>body{margin:0;background:#080808;color:#eee;font:16px Arial;padding:22px}h1{color:#e40521;font-size:23px}.card{border:1px solid #e40521;border-radius:10px;padding:18px;max-width:440px}input,button{width:100%;box-sizing:border-box;margin-top:14px;padding:13px;border-radius:6px}button{background:#e40521;color:#fff;border:0;font-weight:bold}small{color:#aaa}</style></head><body><div class="card"><h1>HONDAPP // TESTE GIF</h1><p>Envie um GIF para testar a animacao no Vehicle Profile.</p><form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" name="media" accept="image/gif" required><button type="submit">ENVIAR GIF DE TESTE</button></form><p><small>Use GIF de ate 10 MB e no maximo 150 x 90 pixels. Este arquivo de teste nao substitui sua foto JPG/PNG.</small></p></div></body></html>)HTML";
const char kPhotoPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>HONDAPP FOTO OU GIF</title><style>body{margin:0;background:#080808;color:#eee;font:16px Arial;padding:22px}h1{color:#e40521;font-size:23px}.card{border:1px solid #e40521;border-radius:10px;padding:18px;max-width:440px}input,button{width:100%;box-sizing:border-box;margin-top:14px;padding:13px;border-radius:6px}button{background:#e40521;color:#fff;border:0;font-weight:bold}small{color:#aaa}</style></head><body><div class="card"><h1>HONDAPP // FOTO OU GIF</h1><p>Envie uma foto JPEG, PNG ou um GIF animado para o Vehicle Profile.</p><form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" name="media" accept="image/jpeg,image/png,image/gif" required><button type="submit">ENVIAR E SUBSTITUIR</button></form><p><small>JPG, PNG ou GIF de ate 10 MB. Para o GIF ficar fluido, prefira ate 150 x 90 pixels. O arquivo anterior so e substituido depois que o novo terminar de salvar.</small></p></div></body></html>)HTML";
const char kBootPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>HONDAPP LOGO</title><style>body{margin:0;background:#080808;color:#eee;font:16px Arial;padding:22px}h1{color:#e40521;font-size:23px}.card{border:1px solid #e40521;border-radius:10px;padding:18px;max-width:440px}input,button{width:100%;box-sizing:border-box;margin-top:14px;padding:13px;border-radius:6px}button{background:#e40521;color:#fff;border:0;font-weight:bold}small{color:#aaa}</style></head><body><div class="card"><h1>HONDAPP // LOGO DE INICIALIZACAO</h1><p>Envie uma imagem JPEG ou PNG para usar como logo EXTRA no cartao microSD.</p><form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" name="media" accept="image/jpeg,image/png" required><button type="submit">ENVIAR LOGO</button></form><p><small>JPG ou PNG, ate 10 MB. Sem cartao ou sem arquivo salvo, o painel inicia automaticamente com a logo Honda padrao.</small></p></div></body></html>)HTML";

String extensionFor(String name) { name.toLowerCase(); if (name.endsWith(".jpg")) return ".jpg"; if (name.endsWith(".jpeg")) return ".jpeg"; if (name.endsWith(".png")) return ".png"; if (name.endsWith(".gif")) return ".gif"; return String(); }
void sendPhotoPage() { server.send_P(200, "text/html", kPhotoPage); }
void sendGifTestPage() { server.send_P(200, "text/html", kGifTestPage); }
void sendBootPage() { server.send_P(200, "text/html", kBootPage); }
void removeAllMedia() { SD_MMC.remove("/vehicle.jpg"); SD_MMC.remove("/vehicle.jpeg"); SD_MMC.remove("/vehicle.png"); SD_MMC.remove("/vehicle.gif"); SD_MMC.remove(kGifTestPath); }

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
    } else if (uploadingBootLogo == 3) {
      // O GIF de teste tem nome próprio: a foto do usuário permanece intocada.
      if (gifOpen) { gif.close(); gifOpen = false; }
    } else {
      // Depois da troca concluída, remove somente os outros formatos. O formato
      // que acabou de chegar permanece intacto — inclusive /vehicle.gif.
      if (uploadTarget != "/vehicle.jpg") SD_MMC.remove("/vehicle.jpg");
      if (uploadTarget != "/vehicle.jpeg") SD_MMC.remove("/vehicle.jpeg");
      if (uploadTarget != "/vehicle.png") SD_MMC.remove("/vehicle.png");
      if (uploadTarget != "/vehicle.gif") SD_MMC.remove("/vehicle.gif");
      refreshSavedMediaPath();
    }
    server.send(200, "text/html", "<body style='background:#080808;color:white;font-family:Arial;padding:25px'><h2>HONDAPP</h2><p>Foto ou GIF salvo no cartao microSD. Reiniciando o painel...</p></body>");
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
    const char *base = uploadingBootLogo == 1 ? "/boot_logo" : (uploadingBootLogo == 2 ? "/boot_logo2" : (uploadingBootLogo == 3 ? "/vehicle_test" : "/vehicle"));
    // O Vehicle Profile aceita JPG, PNG e GIF; as logos de inicialização
    // continuam aceitando somente imagens estáticas.
    if (uploadingBootLogo == 3 && extension != ".gif") return;
    if ((uploadingBootLogo == 1 || uploadingBootLogo == 2) && extension == ".gif") return;
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
  // Remove somente arquivos temporários deixados por um envio interrompido. A mídia
  // concluída, inclusive /vehicle.gif, deve sobreviver ao reinício do painel.
  SD_MMC.remove(kVehicleTemporary); SD_MMC.remove(kVehicleBackup); SD_MMC.remove(kGifTestTemporary); SD_MMC.remove(kGifTestBackup); SD_MMC.remove(kBootTemporary); SD_MMC.remove(kBootBackup); SD_MMC.remove(kBoot2Temporary); SD_MMC.remove(kBoot2Backup);
  refreshSavedMediaPath(); uploadResult = "Cartao microSD pronto para receber JPG, PNG ou GIF."; return true;
}
bool exists() { return sdReady && (savedMediaPath.length() || SD_MMC.exists(kGifTestPath)); }
const char *fileName() { return savedMediaPath.c_str(); }
bool remove() { if (!sdReady) return false; resetGifTest(); invalidateStaticCache(); removeAllMedia(); refreshSavedMediaPath(); return !exists() && !gifTestExists(); }
bool startUploadNetwork() { if (serverActive) return uploadingBootLogo == 0; if (!sdReady || WiFi.status() != WL_CONNECTED) return false; uploadingBootLogo = 0; uploadTemporary = kVehicleTemporary; uploadBackup = kVehicleBackup; server.on("/", HTTP_GET, sendPhotoPage); server.on("/upload", HTTP_POST, completeUpload, receiveUpload); server.begin(); serverActive = true; return true; }
bool gifTestExists() { return sdReady && SD_MMC.exists(kGifTestPath); }
bool startGifTestUploadNetwork() { if (serverActive) return uploadingBootLogo == 3; if (!sdReady || WiFi.status() != WL_CONNECTED) return false; uploadingBootLogo = 3; uploadTemporary = kGifTestTemporary; uploadBackup = kGifTestBackup; server.on("/", HTTP_GET, sendGifTestPage); server.on("/upload", HTTP_POST, completeUpload, receiveUpload); server.begin(); serverActive = true; return true; }
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
bool tick() {
  const unsigned long now = millis();
  if (!gifOpen || !gifOutputTarget || now < gifNextFrameAt) return false;

  // Não espera dentro do loop principal pelo atraso do GIF. No modo assíncrono,
  // AnimatedGIF decodifica somente o próximo quadro e devolve sua duração; o
  // painel continua lendo o toque, atendendo Wi-Fi e animando o áudio entre um
  // quadro e outro. Isso elimina a sensação de atraso no dashboard.
  int frameDelay = 0;
  if (!gif.playFrame(false, &frameDelay)) {
    gif.reset();
    frameDelay = 0;
    if (!gif.playFrame(false, &frameDelay)) return false;
  }
  // O LCD recebe o quadro composto inteiro para preservar transparências e
  // retângulos parciais. Mantém pelo menos 20 ms para não saturar o SPI quando
  // um GIF declarar duração inválida ou muito pequena.
  gifFrame.pushSprite(gifOutputTarget, gifTargetX, gifTargetY);
  if (frameDelay < 20) frameDelay = 20;
  gifNextFrameAt = now + static_cast<unsigned long>(frameDelay);
  return true;
}

void resetGifTest() {
  if (gifOpen) gif.close();
  gifOpen = false;
  gifOutputTarget = nullptr;
  gifFrame.deleteSprite();
  gifTargetWidth = gifTargetHeight = 0;
  gifNextFrameAt = 0;
}

void setGifTarget(lgfx::LGFX_Device &target, int x, int y, int maxWidth, int maxHeight) {
  if (!gifOpen) return;
  gifOutputTarget = &target;
  gifTargetX = x + (maxWidth - gifTargetWidth) / 2;
  gifTargetY = y + (maxHeight - gifTargetHeight) / 2;
}

void clearGifTarget() { gifOutputTarget = nullptr; }

bool pushGifTest(lgfx::LGFX_Device &target, int x, int y, int maxWidth, int maxHeight) {
  setGifTarget(target, x, y, maxWidth, maxHeight);
  return gifOpen;
}

bool drawGifTest(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight) {
  if (!gifTestExists()) return false;
  if (!gifOpen) {
    gif.begin(GIF_PALETTE_RGB565_BE);
    if (!gif.open(kGifTestPath, gifOpenFile, gifCloseFile, gifReadFile, gifSeekFile, gifDraw)) return false;
    // COOKED entrega cada linha já composta sobre os quadros anteriores. Isso
    // preserva as áreas transparentes dos GIFs também no cartão de 150 x 90,
    // onde o modo RAW deixava faixas pretas entre atualizações parciais.
    // O desenho continua limitado somente ao retângulo do Vehicle Profile.
    gif.setDrawType(GIF_DRAW_COOKED);
    const int sourceWidth = gif.getCanvasWidth();
    const int sourceHeight = gif.getCanvasHeight();
    if (sourceWidth <= 0 || sourceHeight <= 0) { gif.close(); return false; }
    const float scale = min((float)maxWidth / sourceWidth, (float)maxHeight / sourceHeight);
    gifTargetWidth = max(1, (int)(sourceWidth * scale + 0.5f));
    gifTargetHeight = max(1, (int)(sourceHeight * scale + 0.5f));
    gifFrame.deleteSprite();
    gifFrame.setColorDepth(16);
    gifFrame.setPsram(true);
    if (!gifFrame.createSprite(gifTargetWidth, gifTargetHeight)) { gif.close(); return false; }
    gifFrame.fillScreen(0x0000);
    gifTargetX = x + (maxWidth - gifTargetWidth) / 2;
    gifTargetY = y + (maxHeight - gifTargetHeight) / 2;
    gifOpen = true;
    gifNextFrameAt = 0;
  }
  // Inclui o último quadro composto no canvas antes de o HUD inteiro ser enviado.
  gifFrame.pushSprite(&target, x + (maxWidth - gifTargetWidth) / 2, y + (maxHeight - gifTargetHeight) / 2);
  return true;
}
// GIF não usa o cache de JPG/PNG. Quando há somente /vehicle.gif,
// savedMediaPath fica vazio; nunca passe esse caminho vazio ao SD_MMC.
bool preloadStatic(int maxWidth, int maxHeight) {
  return savedMediaPath.length() != 0 && drawPath(nullptr, savedMediaPath.c_str(), 0, 0, maxWidth, maxHeight);
}
bool draw(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight) {
  return savedMediaPath.length() != 0 && drawPath(&target, savedMediaPath.c_str(), x, y, maxWidth, maxHeight);
}
} // namespace VehicleMedia
