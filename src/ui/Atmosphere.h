#pragma once
#include <vector>

// Procedural generation of dark-fantasy menu background details: pylons, torches, runes, cracks,
// blood streaks, chains, ribs, obelisks, ritual crosses, curtains, ember swarms, scars, empty eye
// sockets, ash, etc., assembled into a "variant" by DrawDarkFantasyAtmosphere().

namespace MainMenu {

// The content rectangle (title plus buttons), already computed when DrawDarkFantasyAtmosphere is
// called; used so background motifs do not overlap the menu text.
struct AtmosphereBounds { int x0, y0, x1, y1; };

void DrawWornEdgeV(std::vector<unsigned char>& grid, int cols, int rows,
                    int x, int y0, int y1, int seed, bool thin);
void DrawWornEdgeH(std::vector<unsigned char>& grid, int cols, int rows,
                    int y, int x0, int x1, int seed, bool thin);
void DrawRuneCircle(std::vector<unsigned char>& grid, int cols, int rows,
                     int cx, int cy, int radius, int seed);
void DrawCrack(std::vector<unsigned char>& grid, int cols, int rows,
                int startX, int startY, int dirX, int dirY,
                int maxLen, int seed);
void DrawTorch(std::vector<unsigned char>& grid, int cols, int rows,
                int x, int y, int seed);
void DrawPylonCrossbar(std::vector<unsigned char>& grid, int cols, int rows,
                        int pylonX, int frameEdgeY, int towardCol, int seed);
void DrawCornerTick(std::vector<unsigned char>& grid, int cols, int rows,
                     int x, int y, int dx, int dy, int seed);
void DrawTwinRunes(std::vector<unsigned char>& grid, int cols, int rows,
                    int cx, int cy, int spacing, int radius, int seed);

bool AtmosphereFreeCell(int x, int y, int cols, int rows,
                         const AtmosphereBounds& content, int pad = 2);
void SafeAtmosphereGlyph(std::vector<unsigned char>& grid, int cols, int rows,
                          int x, int y, unsigned char glyph,
                          const AtmosphereBounds& content, int pad = 2);

void DrawBloodDrips(std::vector<unsigned char>& grid, int cols, int rows,
                     const AtmosphereBounds& content, int seed,
                     bool fromTop, bool fromBottom, int count);
void DrawHangingChains(std::vector<unsigned char>& grid, int cols, int rows,
                        const AtmosphereBounds& content, int seed,
                        int count, bool bothSides);
void DrawBrokenRibs(std::vector<unsigned char>& grid, int cols, int rows,
                     const AtmosphereBounds& content, int seed,
                     int ribCount, bool topHeavy);
void DrawObelisks(std::vector<unsigned char>& grid, int cols, int rows,
                   const AtmosphereBounds& content, int seed,
                   int count, bool alternateSides);
void DrawRitualCrosses(std::vector<unsigned char>& grid, int cols, int rows,
                        const AtmosphereBounds& content, int seed,
                        int count, bool onSides);
void DrawHangingCurtain(std::vector<unsigned char>& grid, int cols, int rows,
                         const AtmosphereBounds& content, int seed,
                         bool leftSide, bool rightSide);
void DrawEmberSwarm(std::vector<unsigned char>& grid, int cols, int rows,
                     const AtmosphereBounds& content, int seed,
                     int density, bool heavy);
void DrawHorizontalScars(std::vector<unsigned char>& grid, int cols, int rows,
                          const AtmosphereBounds& content, int seed,
                          int count, bool bottomOnly);
void DrawDeadEyes(std::vector<unsigned char>& grid, int cols, int rows,
                   const AtmosphereBounds& content, int seed, int count);
void DrawBloodTrail(std::vector<unsigned char>& grid, int cols, int rows,
                     const AtmosphereBounds& content, int seed, int branches);
void DrawAshfall(std::vector<unsigned char>& grid, int cols, int rows,
                  const AtmosphereBounds& content, int seed, int density);
void DrawDespairLayer(std::vector<unsigned char>& grid, int cols, int rows,
                       const AtmosphereBounds& content, int seed,
                       int variantIndex, int density);

struct AtmosphereVariant {
    bool  pylonLeft = true, pylonRight = true;
    bool  pylonLine = true;
    bool  pylonCrossbar = false;
    bool  torchAtPylonTop = true, torchAtPylonBottom = false;
    int   crackMode = 0;
    int   runeMode = 6;          // 6 = no rune (the default); the ring sigil is a rare motif
    float runeRadiusMul = 1.0f;
    bool  bottomTorch = true, bottomRule = true, bottomDoubleRule = false;
    bool  cornerTicks = false;
    float ashDensityMul = 1.0f;
    int   motifMode = 0;
    int   motifDensity = 0;
    bool  motifBothSides = false;
};

constexpr int kAtmosphereVariantCount = 32;

AtmosphereVariant GetAtmosphereVariant(int index);

int PickAtmosphereVariant(int entryCounter, int seed);

int PickNextAtmosphereVariant(int previousVariant, int entryCounter, int seed);

void DrawDarkFantasyAtmosphere(std::vector<unsigned char>& grid, int cols, int rows,
                                int seed, bool pauseMenu, const AtmosphereBounds& content,
                                int variantIndex = 0);

} // namespace MainMenu
