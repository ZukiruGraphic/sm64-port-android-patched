precision mediump float;
varying vec2 vUV;
uniform sampler2D uTex;
void main() {
  vec4 c = texture2D(uTex, vUV);
  vec2 d = vUV - 0.5;
  float vig = 1.0 - dot(d, d) * 0.45;
  c.rgb *= vig;
  gl_FragColor = c;
}
