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
  // Teste temporário de animação: procura /vehicle_test.gif no microSD.
  bool gifTestExists();
  bool startGifTestUploadNetwork();
  bool drawGifTest(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight);
  // Fecha o decoder antes de mudar entre cartão e tela cheia, para recalcular a escala.
  void resetGifTest();
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
  // Avança no máximo um quadro por chamada e informa quando há pixels novos.
  // Assim o painel envia somente a região do GIF, sem redesenhar os quatro cartões.
  bool tick();
  // Seleciona a região física do LCD que recebe os próximos quadros já compostos.
  void setGifTarget(lgfx::LGFX_Device &target, int x, int y, int maxWidth, int maxHeight);
  // Pausa a saída do GIF fora do Vehicle Profile, sem perder o quadro composto.
  void clearGifTarget();
  // Mantido para compatibilidade com o fluxo do painel; o desenho ocorre em tick().
  bool pushGifTest(lgfx::LGFX_Device &target, int x, int y, int maxWidth, int maxHeight);
  bool preloadStatic(int maxWidth, int maxHeight);
  bool draw(lgfx::LGFX_Sprite &target, int x, int y, int maxWidth, int maxHeight);
}
