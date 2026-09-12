#include "PlayerTorchIcon.h"
#include "render/ShaderLoader.h"
#include "render/ShaderProgram.h"
#include <string>

void PlayerTorchIcon::create()
{
    // Простой квад -1..1 (два треугольника) — вся форма факела рисуется
    // процедурно во фрагментном шейдере (см. player_torch_icon.frag), не
    // самой геометрией.
    const float quad[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,

        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindVertexArray(0);

    const std::string vertSrc = ShaderLoader::LoadSource("assets/shaders/player_torch_icon.vert");
    const std::string fragSrc = ShaderLoader::LoadSource("assets/shaders/player_torch_icon.frag");
    GLuint vs = ShaderProgram::CompileShader(GL_VERTEX_SHADER, vertSrc.c_str(), "PlayerTorchIcon");
    GLuint fs = ShaderProgram::CompileShader(GL_FRAGMENT_SHADER, fragSrc.c_str(), "PlayerTorchIcon");
    m_program = ShaderProgram::LinkProgram(vs, fs, "PlayerTorchIcon");

    cacheUniformLocations();
}

void PlayerTorchIcon::cacheUniformLocations()
{
    if (!m_program)
        return;

    m_uniIconCenter   = glGetUniformLocation(m_program, "iconCenter");
    m_uniIconHalfSize = glGetUniformLocation(m_program, "iconHalfSize");
    m_uniTime         = glGetUniformLocation(m_program, "uTime");
}

void PlayerTorchIcon::destroy()
{
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
}

void PlayerTorchIcon::render(int viewportWidth, int viewportHeight, float timeSeconds)
{
    if (!m_program || viewportWidth <= 0 || viewportHeight <= 0)
        return;

    glViewport(0, 0, viewportWidth, viewportHeight);

    // Иконка рисуется поверх уже готового ASCII-кадра, полупрозрачными
    // краями (альфа с discard в шейдере) — без теста глубины (это чистый
    // 2D HUD-элемент, ему нечего сравнивать по глубине) и с блендингом.
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_program);

    // Нижний левый угол экрана, с отступом от края. aspect-поправка по X,
    // чтобы форма факела не растягивалась/сжималась на неквадратных
    // разрешениях (NDC X и Y соответствуют разному числу физических
    // пикселей, если ширина окна != высоте — тот же принцип, что и у
    // компаса/остальных экранных оверлеев).
    const float aspect = (float)viewportWidth / (float)viewportHeight;
    const float halfSizeY = 0.22f;
    const float halfSizeX = halfSizeY * 0.55f / aspect;

    glUniform2f(m_uniIconCenter, -0.74f, -0.60f);
    glUniform2f(m_uniIconHalfSize, halfSizeX, halfSizeY);
    glUniform1f(m_uniTime, timeSeconds);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
