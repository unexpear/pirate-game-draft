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
	float ambient = 0.42;
	vec3 col = v_color.xyz * (ambient + (1.0 - ambient) * ndl);
	gl_FragColor = vec4(col, 1.0);
}
