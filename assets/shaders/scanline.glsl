// scanline.glsl — 단순 스캔라인 셰이더 (테스트용)
// FBNeoRageX 내장 번들 셰이더
// 형식: fragment-only (vertex는 기본 passthrough 사용)

#version 120
uniform sampler2D uTex;
uniform vec4      TextureSize;    // (w, h, 1/w, 1/h)
varying vec2      vUV;

void main() {
    vec4 col = texture2D(uTex, vUV);

    // 스캔라인: 짝수 행을 살짝 어둡게
    float line = mod(floor(vUV.y * TextureSize.y), 2.0);
    float dim  = mix(0.70, 1.0, line);   // 어두운 행 70%
    col.rgb *= dim;

    gl_FragColor = col;
}
