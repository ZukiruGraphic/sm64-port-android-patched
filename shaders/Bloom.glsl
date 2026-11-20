precision mediump float;
varying vec2 vUV;
uniform sampler2D uTex;
uniform sampler2D uLowTex;
uniform sampler2D uPrevLowTex0;
uniform sampler2D uPrevLowTex1;

void main() {
  vec4 base = texture2D(uTex, vUV);

  vec4 bloomNow = texture2D(uLowTex, vUV);
  vec4 bloomPrev0 = texture2D(uPrevLowTex0, vUV);
  vec4 bloomPrev1 = texture2D(uPrevLowTex1, vUV);
  float ghost = 0.5;
  vec4 bloom = mix(bloomNow, bloomPrev0, ghost);
  bloom = mix(bloom, bloomPrev1, ghost);

  float b = pow(max(max(bloom.r, bloom.g), bloom.b), 5.0);
  
  base = pow(base, vec4(2.2));
  base = mix(base, base * 0.6 + bloom * b, 0.35);
  base *= 0.85;
  base = pow(base, vec4(1.0/2.2));
  
  gl_FragColor = base;
}