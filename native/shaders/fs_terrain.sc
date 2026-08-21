$input v_normal, v_color, v_wpos

// Terrain fragment shader: directional N.L plus ambient, tinted by the baked
// per-vertex colour (sand at the waterline, grass inland, rock on the heights).
#include <bgfx_shader.sh>

uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_camPos;   // xyz = eye position (for distance fog)
uniform vec4 u_fog;      // xyz = horizon fog colour, w = fog far distance
uniform mat4 u_lightMtx; // light view-proj, for shadow lookup
SAMPLER2D(s_shadowMap, 4);

float shadowFactor(vec3 wpos) {
	vec4 lc = mul(u_lightMtx, vec4(wpos, 1.0));
	vec3 ndc = lc.xyz / lc.w;
	vec2 uv = ndc.xy * 0.5 + 0.5;
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_PSSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV
	uv.y = 1.0 - uv.y;
#endif
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
	float cur = ndc.z - 0.0025;
	float texel = 1.0 / 1024.0;
	float sh = 0.0;
	for (int y = -1; y <= 1; ++y)
	for (int x = -1; x <= 1; ++x)
		sh += (cur > texture2DLod(s_shadowMap, uv + vec2(float(x), float(y)) * texel, 0.0).x) ? 1.0 : 0.0;
	return 1.0 - (sh / 9.0) * 0.30;
}

void main()
{
	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	float ndl = max(dot(N, L), 0.0);
	// Warm sun + cool sky fill, matching the mesh shader.
	vec3 sun = vec3(1.22, 1.10, 0.90);
	vec3 skyfill = vec3(0.56, 0.64, 0.72);
	vec3 col = v_color.xyz * (skyfill * 0.66 + sun * ndl * 0.82);

	col *= shadowFactor(v_wpos); // receive cast shadows

	// Aerial-perspective fog: the island recedes into the horizon haze with
	// distance, so it reads as land rooted in a receding sea, not a flat prop.
	if (u_fog.w > 1.0)
	{
		float dist = length(u_camPos.xyz - v_wpos);
		float fog = smoothstep(u_fog.w * 0.32, u_fog.w, dist); // island recedes into haze
		col = mix(col, u_fog.xyz, fog);
	}

	gl_FragColor = vec4(col, 1.0);
}
