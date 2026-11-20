precision mediump float;
varying vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uTexelSize;
const float u_sharpness = 8.0;

void main() {
    vec2 p = uTexelSize * 2.0;

    vec3 c = texture2D(uTex, vUV).rgb;

    vec3 up    = texture2D(uTex, vUV + vec2(0.0, -p.y)).rgb;
    vec3 down  = texture2D(uTex, vUV + vec2(0.0,  p.y)).rgb;
    vec3 left  = texture2D(uTex, vUV + vec2(-p.x, 0.0)).rgb;
    vec3 right = texture2D(uTex, vUV + vec2( p.x, 0.0)).rgb;

    vec3 a = texture2D(uTex, vUV + vec2(-p.x, -p.y)).rgb;
    vec3 b = texture2D(uTex, vUV + vec2( p.x, -p.y)).rgb;
    vec3 d = texture2D(uTex, vUV + vec2(-p.x,  p.y)).rgb;
    vec3 e = texture2D(uTex, vUV + vec2( p.x,  p.y)).rgb;

    vec3 avg = (up + down + left + right) * 0.25;
    vec3 diff = c - avg;

    vec3 mn = min(min(min(a, b), min(d, e)), c);
    vec3 mx = max(max(max(a, b), max(d, e)), c);
    vec3 range = mx - mn;
    mn -= range * 0.2;
    mx += range * 0.2;

    vec3 result = c + diff * u_sharpness;
    result = mix(result, clamp(result, mn, mx), 1.0);
    result = clamp(result, 0.0, 1.0);

    gl_FragColor = vec4(result, 1.0);
}