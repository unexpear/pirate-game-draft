$input v_normal, v_color, v_wpos

// Terrain fragment shader: directional N.L plus ambient, tinted by the baked
// per-vertex colour (sand at the waterline, grass inland, rock on the heights).
#include <bgfx_shader.sh>

uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_camPos;   // xyz = eye position (for distance fog)
uniform vec4 u_fog;      // xyz = horizon fog colour, w = fog far distance

void main()
{
	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	float ndl = max(dot(N, L), 0.0);
	// Warm sun + cool sky fill, matching the mesh shader.
	vec3 sun = vec3(1.22, 1.10, 0.90);
	vec3 skyfill = vec3(0.56, 0.64, 0.72);
	vec3 col = v_color.xyz * (skyfill * 0.66 + sun * ndl * 0.82);

	// Aerial-perspective fog: the island recedes into the horizon haze with
	// distance, so it reads as land rooted in a receding sea, not a flat prop.
	if (u_fog.w > 1.0)
	{
		float dist = length(u_camPos.xyz - v_wpos);
		float fog = smoothstep(u_fog.w * 0.5, u_fog.w, dist);
		col = mix(col, u_fog.xyz, fog);
	}

	gl_FragColor = vec4(col, 1.0);
}
