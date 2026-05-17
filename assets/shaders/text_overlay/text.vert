#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(push_constant) uniform TextPushConstants {
    vec2 screenSize;
} pushConstants;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

void main() {
    vec2 normalized = vec2(
        (inPosition.x / pushConstants.screenSize.x) * 2.0 - 1.0,
        (inPosition.y / pushConstants.screenSize.y) * 2.0 - 1.0
    );

    gl_Position = vec4(normalized, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragColor = inColor;
}