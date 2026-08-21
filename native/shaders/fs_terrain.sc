$input v_normal, v_color

// Terrain fragment shader: directional N.L plus ambient, tinted by the baked
// per-vertex colour (sand at the waterline, grass inland, rock on the heights).
#include <bgfx_shader.sh>

uniform vec4 u_lightDir; // xyz = direction to the sun

void main()
{
	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	float ndl = max(dot(N, L), 0.0);
	// Warm sun + cool sky fill, matching the mesh shader.
	vec3 sun = vec3(1.15, 1.00, 0.80);
	vec3 skyfill = vec3(0.50, 0.56, 0.62);
	vec3 col = v_color.xyz * (skyfill * 0.55 + sun * ndl * 0.80);
	gl_FragColor = vec4(col, 1.0);
}
