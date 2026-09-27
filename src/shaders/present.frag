#version 450

// Vulkan port of the original presentDepthHLSL pixel shader, minus the depth output -
// WiVRn doesn't support XR_KHR_composition_layer_depth (confirmed via hello_xr
// diagnostics against this exact runtime), so there's nothing to submit depth to.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D g_colorTexture;
layout(binding = 1) uniform sampler2D g_fadeSampleTexture;

layout(binding = 2) uniform Settings {
    float renderWidth;
    float renderHeight;
    float swapchainWidth;
    float swapchainHeight;
    float uvOffsetX;
    float uvOffsetY;
    float uvScaleX;
    float uvScaleY;
    float customFadeAmount;
    float customFadeColorR;
    float customFadeColorG;
    float customFadeColorB;
    float isFadeActive;
} settings;

void main() {
    vec2 samplePosition = vec2(settings.uvOffsetX, settings.uvOffsetY) + inUV * vec2(settings.uvScaleX, settings.uvScaleY);

    vec4 colorTexture = texture(g_colorTexture, samplePosition);

    // Game fade: sample the corner texel directly from the 2D layer texture
    vec4 fadeSample = texelFetch(g_fadeSampleTexture, ivec2(0, 0), 0);
    float gameFade = settings.isFadeActive > 0.5 ? fadeSample.a : 0.0;
    vec3 gameFadeColor = fadeSample.rgb;

    // Custom fade (programmatic, e.g. for turn snapping)
    float customFade = clamp(settings.customFadeAmount, 0.0, 1.0);
    vec3 customFadeCol = vec3(settings.customFadeColorR, settings.customFadeColorG, settings.customFadeColorB);

    // Composite: whichever fade is stronger wins
    float finalFade = max(gameFade, customFade);
    vec3 finalFadeColor = gameFade >= customFade ? gameFadeColor : customFadeCol;

    outColor = vec4(mix(colorTexture.rgb, finalFadeColor, finalFade), colorTexture.a);
}
