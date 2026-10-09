#pragma once
#include <string>
#include <vector>

// Text on a black screen in the big font. Each letter assembles from scattered cells, one letter after another
// An empty line ends a stanza: the stanza holds, dissolves, and the next one starts on a clean screen
class StoryText {
public:
    void start(const std::vector<std::string>& lines, int seed);
    void update(float deltaTime);
    void skip() { m_stanza = (int)m_stanzas.size(); }
    bool finished() const { return m_stanza >= (int)m_stanzas.size(); }

    // grid uses the menu grid layout (glyph index + 1 per cell, 0 = empty)
    void draw(std::vector<unsigned char>& grid, int cols, int rows) const;

private:
    // Seconds from the start of a stanza
    float lineStart(int line) const;
    float stanzaFullyShown() const;

    std::vector<std::vector<std::string>> m_stanzas;
    int m_stanza = 0;
    float m_time = 0.0f;
    int m_seed = 0;
    int m_maxLineLength = 1;
    int m_maxStanzaLines = 1;
};
