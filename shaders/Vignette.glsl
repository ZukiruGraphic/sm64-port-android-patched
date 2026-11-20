precision mediump float;
varying vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uResolution;

void main() {
  vec4 c = texture2D(uTex, vUV);

  vec2 uv = vUV - 0.5;
  uv.x *= uResolution.x / uResolution.y;
  float dist = length(uv);
  
  float vig = smoothstep(1.5, 0.2, dist);
  vig = clamp(mix(1.0, vig, 0.5), 0.0, 1.0);

  c.rgb *= vig;
  
  gl_FragColor = c;
}
