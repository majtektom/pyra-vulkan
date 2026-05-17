#version 450

layout(binding = 0) uniform sampler2D fontAtlasSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    float coverage = texture(fontAtlasSampler, fragTexCoord).r;
    outColor = vec4(fragColor.rgb, fragColor.a * coverage);
}