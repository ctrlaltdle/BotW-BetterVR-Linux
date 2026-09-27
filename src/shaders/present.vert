#version 450

// Generates a full-screen quad from a 4-vertex, no-vertex-buffer draw (matching the
// original D3D12 shader's approach: 2 triangles via indices {0,1,2, 2,1,3}).
layout(location = 0) out vec2 outUV;

void main() {
    outUV = vec2(gl_VertexIndex % 2, (gl_VertexIndex % 4) / 2);
    // No Y negation here: Vulkan's NDC/clip space has +Y pointing DOWN (opposite of D3D,
    // which has +Y pointing UP). The original D3D12 shader this was ported from negated Y
    // to make UV.y=0 (texture top) land at NDC top under D3D's Y-up convention - copying
    // that formula unmodified into Vulkan flipped every image upside down instead, since
    // Vulkan already puts UV.y=0 at NDC Y=-1 (top) without any negation needed. Confirmed
    // live: the menu/HUD layer rendered upside-down with the negation in place.
    gl_Position = vec4((outUV.x - 0.5) * 2.0, (outUV.y - 0.5) * 2.0, 0.0, 1.0);
}
