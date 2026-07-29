#version 450

layout(location = 0) out vec2 v_texCoord;

const vec2 POSITIONS[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0));

const vec2 TEX_COORDS[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0));

void main()
{
    gl_Position = vec4(POSITIONS[gl_VertexIndex], 0.0, 1.0);
    v_texCoord = TEX_COORDS[gl_VertexIndex];
}
