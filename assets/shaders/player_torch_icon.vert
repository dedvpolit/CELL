#version 330 core

// Простой экранный квад — БЕЗ view/projection вообще (в отличие от
// Compass, который рисует "приколотый" к экрану, но всё же 3D-объект).
// aPos — локальные координаты квада, -1..1 по обеим осям.
layout(location = 0) in vec2 aPos;

uniform vec2 iconCenter;   // центр иконки в NDC (-1..1)
uniform vec2 iconHalfSize; // половина размера иконки в NDC (уже с поправкой на аспект — см. C++ сторону)

out vec2 vUV; // локальные 0..1 координаты внутри иконки, для фрагментного шейдера

void main()
{
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(iconCenter + aPos * iconHalfSize, 0.0, 1.0);
}
