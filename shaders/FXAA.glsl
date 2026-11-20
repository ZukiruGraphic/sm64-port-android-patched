precision mediump float;
varying vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uTexelSize;

void main() {
  vec4 c = texture2D(uTex, vUV);
  vec4 n = texture2D(uTex, vUV + vec2(0.0, uTexelSize.y));
  vec4 s = texture2D(uTex, vUV - vec2(0.0, uTexelSize.y));
  vec4 e = texture2D(uTex, vUV + vec2(uTexelSize.x, 0.0));
  vec4 w = texture2D(uTex, vUV - vec2(uTexelSize.x, 0.0));
  
  vec4 ne = texture2D(uTex, vUV + uTexelSize);
  vec4 sw = texture2D(uTex, vUV - uTexelSize);

  float lum = (c.r + c.g + c.b) * 0.3333;
  float lumN = (n.r + n.g + n.b) * 0.3333;
  float lumS = (s.r + s.g + s.b) * 0.3333;
  float edge = abs(lumN - lum) + abs(lumS - lum);

  float mixAmt = clamp(edge * 6.0, 0.0, 0.8);
  vec4 avg = (n + s + e + w + ne + sw) * 0.1666;
  
  gl_FragColor = mix(c, avg, mixAmt);
}