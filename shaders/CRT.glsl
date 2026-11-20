precision mediump float;
varying vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uResolution;

void main() {
  vec2 cc = vUV - 0.5;
  float r2 = dot(cc, cc);
  vec2 uv = vUV + cc * r2 * 0.08;
  
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec4 c = texture2D(uTex, uv);

  float scan = 0.85 + 0.15 * sin(uv.y * uResolution.y * 3.14159);
  c.rgb *= scan;

  float grille = 0.92 + 0.08 * sin(uv.x * uResolution.x * 3.14159);
  c.rgb *= grille;

  vec2 vc = uv - 0.5;
  vc.x *= uResolution.x / uResolution.y;
  float vig = smoothstep(1.15, 0.65, length(vc));
  c.rgb *= mix(1.0, vig, 0.55);

  c.rgb = pow(c.rgb, vec3(1.15));

  gl_FragColor = c;
}