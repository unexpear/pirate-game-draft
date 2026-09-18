$input v_normal, v_color, v_wpos

// Terrain fragment shader: the baked per-vertex colour (sand at the waterline,
// grass inland, rock on the heights) lit by the same warm sun and hemispheric
// sky/ground ambient as the meshes, and receiving the town's cast shadows.
#include <bgfx_shader.sh>

uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_camPos;   // xyz = eye position (for distance fog)
uniform vec4 u_fog;      // xyz = horizon fog colour, w = fog far distance
uniform mat4 u_lightMtx; // light view-proj, for shadow lookup
SAMPLER2D(s_shadowMap, 4);

// Fraction of the sun reaching this point (1 = full sun, 0 = shadowed), 3x3 PCF.
float sunVisibility(vec3 wpos) {
	vec4 lc = mul(u_lightMtx, vec4(wpos, 1.0));
	vec3 ndc = lc.xyz / lc.w;
	vec2 uv = ndc.xy * 0.5 + 0.5;
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_PSSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV
	uv.y = 1.0 - uv.y;
#endif
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
	float cur = ndc.z - 0.0006;
	float texel = 1.0 / 2048.0;
	float sh = 0.0;
	for (int y = -1; y <= 1; ++y)
	for (int x = -1; x <= 1; ++x)
		sh += (cur > texture2DLod(s_shadowMap, uv + vec2(float(x), float(y)) * texel, 0.0).x) ? 1.0 : 0.0;
	return 1.0 - sh / 9.0;
}

// Same soft highlight shoulder as the meshes (below 0.82 untouched).
vec3 shoulder(vec3 c) {
	vec3 over = max(c - vec3_splat(0.82), vec3_splat(0.0));
	return min(c, vec3_splat(0.82)) + vec3_splat(0.18) * (vec3_splat(1.0) - exp(-over / 0.18));
}

void main()
{
	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	float ndl = max(dot(N, L), 0.0);
	// Buildings and the ship now shadow the ground; shade keeps the sky fill.
	float vis = sunVisibility(v_wpos + N * 0.30);
	vec3 sun = vec3(1.26, 1.13, 0.92);
	vec3 sky = vec3(0.52, 0.64, 0.82);
	vec3 gnd = vec3(0.50, 0.44, 0.35);
	vec3 amb = mix(gnd, sky, N.y * 0.5 + 0.5) * 0.74;
	vec3 col = v_color.xyz * (amb + sun * (ndl * vis * 0.86));
	col = shoulder(col);

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
