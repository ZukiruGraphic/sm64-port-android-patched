precision mediump float;
varying vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uResolution;
uniform vec2 uTexelSize;

void main() {
  vec4 c = texture2D(uTex, vUV);
  vec3 col = c.rgb;
  col *= 0.98;
 
  float lum = dot(col, vec3(0.299, 0.587, 0.114));

  float shadowW = 1.0 - smoothstep(0.0, 0.6, lum);
  float highlightW = smoothstep(0.25, 0.85, lum);
  float mask = max(shadowW, highlightW);

  float reinhardLum = lum / (1.0 + lum * 0.35) * 1.35;
  reinhardLum = reinhardLum * 0.95 + 0.02 * shadowW;

  float newLum = mix(lum, reinhardLum, mask * 1.0);

  float ratio = newLum / max(lum, 0.0001);
 
  col *= ratio;
  col = pow(col, vec3(1.05));

  gl_FragColor = vec4(col, 1.0);
}