#include "StoryText.h"
#include "BigFont.h"
#include "TextGrid.h"
#include "UiGlyphs.h"
#include <algorithm>
#include <cctype>

namespace {

constexpr float kLetterInterval = 0.07f;  // between letter starts
constexpr float kAssembleTime = 0.35f;    // a letter's cells appear spread over this
constexpr float kLinePause = 0.5f;
constexpr float kStanzaHold = 1.6f;       // after the last letter is complete
constexpr float kLastStanzaHold = 2.4f;
constexpr float kDissolveTime = 0.6f;

float Hash01(int x, int seed) { return (float)(MainMenu::Hash(x, seed) % 1000u) / 1000.0f; }

} // namespace

void StoryText::start(const std::vector<std::string>& lines, int seed)
{
    m_stanzas.clear();
    m_stanzas.emplace_back();
    for (const std::string& raw : lines)
    {
        if (raw.empty())
        {
            if (!m_stanzas.back().empty())
                m_stanzas.emplace_back();
            continue;
        }
        // The big font has capitals only.
        std::string line = raw;
        for (char& c : line)
            c = (char)std::toupper((unsigned char)c);
        m_stanzas.back().push_back(line);
    }
    if (m_stanzas.back().empty())
        m_stanzas.pop_back();

    m_maxLineLength = 1;
    m_maxStanzaLines = 1;
    for (const auto& stanza : m_stanzas)
    {
        m_maxStanzaLines = std::max(m_maxStanzaLines, (int)stanza.size());
        for (const std::string& line : stanza)
            m_maxLineLength = std::max(m_maxLineLength, (int)line.size());
    }

    m_stanza = 0;
    m_time = 0.0f;
    m_seed = seed;
}

float StoryText::lineStart(int line) const
{
    float t = 0.0f;
    const auto& stanza = m_stanzas[(size_t)m_stanza];
    for (int i = 0; i < line; ++i)
        t += (float)stanza[(size_t)i].size() * kLetterInterval + kAssembleTime + kLinePause;
    return t;
}

float StoryText::stanzaFullyShown() const
{
    const auto& stanza = m_stanzas[(size_t)m_stanza];
    const int last = (int)stanza.size() - 1;
    return lineStart(last) + (float)stanza[(size_t)last].size() * kLetterInterval + kAssembleTime;
}

void StoryText::update(float deltaTime)
{
    if (finished())
        return;
    m_time += deltaTime;
    const bool lastStanza = m_stanza + 1 == (int)m_stanzas.size();
    const float end = stanzaFullyShown() + (lastStanza ? kLastStanzaHold : kStanzaHold) + kDissolveTime;
    if (m_time >= end)
    {
        ++m_stanza;
        m_time = 0.0f;
    }
}

void StoryText::draw(std::vector<unsigned char>& grid, int cols, int rows) const
{
    std::fill(grid.begin(), grid.end(), (unsigned char)0);
    if (finished())
        return;

    // The largest cell size at which the longest line and the tallest stanza still fit.
    int finalRes = 1;
    for (int res = 4; res >= 1; --res)
    {
        const int width = m_maxLineLength * 6 * res - res;
        const int height = m_maxStanzaLines * 7 * res + (m_maxStanzaLines - 1) * 3 * res;
        if (width <= cols * 9 / 10 && height <= rows * 8 / 10)
        {
            finalRes = res;
            break;
        }
    }
    const float scale = (float)finalRes / (float)MainMenu::kMaskUpsample;
    const int letterW = 5 * finalRes;
    const int letterStep = letterW + finalRes;
    const int lineStep = 7 * finalRes + 3 * finalRes;

    const auto& stanza = m_stanzas[(size_t)m_stanza];
    const int blockH = (int)stanza.size() * lineStep - 3 * finalRes;
    const int top = (rows - blockH) / 2;

    const bool lastStanza = m_stanza + 1 == (int)m_stanzas.size();
    const float dissolveStart = stanzaFullyShown() + (lastStanza ? kLastStanzaHold : kStanzaHold);
    const int stanzaSeed = m_seed + m_stanza * 104729;

    for (size_t li = 0; li < stanza.size(); ++li)
    {
        const std::string& line = stanza[li];
        const int left = (cols - MainMenu::BigTextWidth(line, scale)) / 2;
        const int rowTop = top + (int)li * lineStep;
        const float lineT0 = lineStart((int)li);

        for (size_t ci = 0; ci < line.size(); ++ci)
        {
            const float letterT0 = lineT0 + (float)ci * kLetterInterval;
            if (m_time < letterT0)
                break;
            const MainMenu::BigGlyph& glyph = MainMenu::GetBigGlyph(line[ci]);
            const int letterSeed = stanzaSeed + (int)(li * 977 + ci * 131);
            for (int r = 0; r < 7; ++r)
            {
                for (int c = 0; c < 5; ++c)
                {
                    if (glyph.rows[r][c] != '#')
                        continue;
                    const int pixel = r * 5 + c;
                    if (m_time < letterT0 + Hash01(pixel, letterSeed) * kAssembleTime)
                        continue;
                    if (m_time > dissolveStart + Hash01(pixel, letterSeed + 7) * kDissolveTime)
                        continue;
                    for (int sy = 0; sy < finalRes; ++sy)
                    {
                        for (int sx = 0; sx < finalRes; ++sx)
                        {
                            const int gc = left + (int)ci * letterStep + c * finalRes + sx;
                            const int gr = rowTop + r * finalRes + sy;
                            MainMenu::PutGlyph(grid, cols, rows, gc, gr,
                                               MainMenu::PickDenseGlyph(gc, gr, letterSeed));
                        }
                    }
                }
            }
        }
    }
}
