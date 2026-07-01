$input v_normal, v_wpos

// Water fragment shader: depth-shaded blue with a fresnel rim and foam on the
// crests. Deliberately simple — a readable ocean, not a AAA one.
#include <bgfx_shader.sh>

uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_camPos;   // xyz = eye position

void main()
{
	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	float ndl = max(dot(N, L), 0.0);

	vec3 deep = vec3(0.02, 0.12, 0.20);
	vec3 lit  = vec3(0.10, 0.42, 0.55);
	vec3 col  = mix(deep, lit, ndl);

	vec3 V = normalize(u_camPos.xyz - v_wpos);
	float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
	col += fres * vec3(0.25, 0.35, 0.45);

	// Foam near the crests.
	float foam = smoothstep(0.55, 0.90, v_wpos.y);
	col = mix(col, vec3(0.85, 0.92, 1.0), foam * 0.7);

	gl_FragColor = vec4(col, 1.0);
}
