// greyscale.glsl — 흑백 변환 셰이더 (시각적 효과 확인용)
// FBNeoRageX 내장 번들 셰이더
// 형식: fragment-only (vertex는 기본 passthrough 사용)

#version 120
uniform sampler2D uTex;
varying vec2      vUV;

void main() {
    vec4 col  = texture2D(uTex, vUV);
    float lum = dot(col.rgb, vec3(0.299, 0.587, 0.114));
    gl_FragColor = vec4(vec3(lum), col.a);
}
