// SPDX-License-Identifier: MIT
// DualMesh – firmwareversion og projektoplysninger (vises på "Omkring"-siden i Indst. > System).
// Hæv DM_FW_VERSION ved hver udgivelse (semantisk version). DM_GIT_REV sættes af
// tools/git_rev.py ved bygning (kort commit-hash, "+" = ændringer, der ikke er committet).
#pragma once

#define DM_FW_VERSION "0.1.6"
#ifndef DM_GIT_REV
#define DM_GIT_REV "ukendt"
#endif
#define DM_AUTHOR "GeekGarage.dk"
#define DM_PROJECT_URL "https://github.com/geekgarage/dualmesh"
#define DM_PROJECT_URL_SHORT "github.com/geekgarage/dualmesh"
#define DM_LICENSE "MIT"
// Fejlrapport-siden (docs/rapport/ via GitHub Pages; virker først, når repoet er offentligt).
#define DM_REPORT_URL "https://geekgarage.github.io/dualmesh/rapport/"
#define DM_REPORT_URL_SHORT "geekgarage.github.io/dualmesh/rapport"
