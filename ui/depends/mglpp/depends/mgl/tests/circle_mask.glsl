uniform sampler2D texture;
uniform vec2 resolution;

vec4 circle(vec2 uv, vec2 pos, float rad, vec4 color) {
    float d = smoothstep(0.0, 2.0, length(pos - uv) - rad);
    float t = clamp(d, 0.0, 1.0);
    return vec4(color.rgb, color.a * (1.0 - t));
}

void main() {
    vec2 uv = gl_TexCoord[0].xy * resolution;
    vec2 center = resolution * 0.5;
    float radius = 0.5 * resolution.y - 1.0;

    vec4 texture_color = texture2D(texture, gl_TexCoord[0].xy);
    gl_FragColor = circle(uv, center, radius, texture_color);
}