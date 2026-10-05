# HondApp

  📂 HondApp/
   ├── 📄 platformio.ini
   ├── 📄 partitions.csv
   │
   ├── 📂 src/
   │    ├── 📄 main.cpp <--[cite: 2] Seu código principal do painel Honda
   │    └── 📄 settings_store.cpp <--[cite: 2] Suas lógicas e funções
   ├── 📂 include/
   │    ├── 📄 lv_conf.h <-- Suas configurações ou arquivos .h
   │    ├── 📄 settings_store.h <-- Seus arquivos de cabeçalho
   │    ├── 📄 hondapp_config.h
   │    └── 📄 hondapp_display.h
   ├── 📂 .github/
   │    └── 📂 workflows/
   │         └── 📄release.yml <-- Script do GitHub Actions para compilar
   │
   └── 📄 README.md <-- Descrição do repositório