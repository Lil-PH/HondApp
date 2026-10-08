#pragma once

#include <Arduino.h>
#include <SD_MMC.h>
#include <LovyanGFX.hpp>

// Fotos JPEG e PNG enviadas pelo navegador ao cartão microSD integrado.
namespace VehicleMedia {
  bool begin();
  bool exists();
  const char *fileName();
  bool remove();
  bool startUploadNetwork();
  // Logo EXTRA1 opcional para a abertura. Ela vem do microSD; sem cartão ou arquivo,
  // o painel mantém a logo Honda padrão.
  bool bootLogoExists();
  bool bootLogo2Exists();
  bool startBootLogoUploadNetwork();
  bool startBootLogo2UploadNetwork();
  // Prepara a logo selecionada na PSRAM antes da animação de abertura.
  // Assim a primeira tela não precisa decodificar JPG/PNG no meio da barra.
  bool preloadBootLogo(int maxWidth, int maxHeight);
  bool preloadBootLogo2(int maxWidth, int maxHeight);
  bool drawBootLogo(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight);
  bool drawBootLogo2(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight);
  void stopUploadNetwork();
  bool uploadNetworkActive();
  void handleNetwork();
  String uploadAddress();
  String status();
  // Mantido sem ação para preservar as chamadas já existentes no painel.
  void tick();
  bool preloadStatic(int maxWidth, int maxHeight);
  bool draw(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight);
}
