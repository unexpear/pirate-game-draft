$input v_normal

// Lit-mesh fragment shader: directional N.L plus ambient, tinted by u_color.
#include <bgfx_shader.sh>

uniform vec4 u_color;    // rgb = piece color
uniform vec4 u_lightDir; // xyz = direction to the sun

void main()
{
	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	float ndl = max(dot(N, L), 0.0);
	float ambient = 0.35;
	vec3 col = u_color.xyz * (ambient + (1.0 - ambient) * ndl);
	gl_FragColor = vec4(col, 1.0);
}
