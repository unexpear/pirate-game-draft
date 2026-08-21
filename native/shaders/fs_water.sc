$input v_normal, v_wpos, v_worldxz

// Water fragment shader: depth-shaded blue with a fresnel rim and foam on the
// crests. The sea is CUT OUT where land is (u_landCut), with foam at the shore.
#include <bgfx_shader.sh>

uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_camPos;   // xyz = eye position
uniform vec4 u_landCut;  // xy = land centre (world), z = radius, w unused
uniform vec4 u_fog;      // xyz = horizon fog colour, w = fog far distance
uniform vec4 u_ship;     // xy = heading (sin,cos), z = hull half-length, w = hull half-beam
uniform vec4 u_shipDyn;  // x = speed factor (0..1) for bow wave + wake strength
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
	return 1.0 - (sh / 9.0) * 0.38;
}

void main()
{
	// Carve the ocean out under the island so land isn't drawn over water.
	float landD = length(v_worldxz.xy - u_landCut.xy);
	if (u_landCut.z > 0.5 && landD < u_landCut.z) discard;

	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	float ndl = max(dot(N, L), 0.0);

	vec3 deep = vec3(0.02, 0.12, 0.20);
	vec3 lit  = vec3(0.10, 0.42, 0.55);
	vec3 col  = mix(deep, lit, ndl);

	vec3 toEye = u_camPos.xyz - v_wpos;
	vec3 V = normalize(toEye);
	float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
	col += fres * vec3(0.25, 0.35, 0.45);

	// Sun specular glint on the crests, toward the light — tight and subtle so it
	// sparkles rather than blowing out the coarse grid facets.
	vec3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), 90.0);
	col += spec * vec3(1.0, 0.95, 0.80) * 0.28; // sun-glint track toward the light

	// Foam clings to the STEEP faces of crests (surface slope), not scattered by
	// height — so it streaks the sharp crest edges instead of floating in blobs.
	float slope = length(N.xz);                       // 0 flat .. larger on steep crest faces
	float foam = smoothstep(0.22, 0.30, slope) * smoothstep(0.28, 0.5, v_wpos.y); // tighter -> crisper crest edges
	col = mix(col, vec3(0.90, 0.95, 1.0), foam * 0.85);

	// Caribbean shallows: a wide turquoise band where the shelf shoals up to the
	// island, then a bright foam line right at the shore.
	if (u_landCut.z > 0.5)
	{
		float shallow = 1.0 - smoothstep(u_landCut.z, u_landCut.z + 30.0, landD);
		col = mix(col, vec3(0.16, 0.68, 0.66), shallow * shallow * 0.6); // turquoise
		float shore = 1.0 - smoothstep(u_landCut.z, u_landCut.z + 4.0, landD);
		col = mix(col, vec3(0.85, 0.93, 0.95), shore * 0.6);
	}

	col *= shadowFactor(v_wpos); // the ship casts a shadow on the sea

	// --- The ship's interaction with the sea (it sits at the scene origin,
	// oriented by heading): a foam ring at the waterline, a bow wave, and a
	// trailing V-wake, so the hull displaces water instead of clipping/hovering.
	if (u_ship.z > 0.1)
	{
		float sH = u_ship.x, cH = u_ship.y, hl = u_ship.z, hb = u_ship.w;
		vec2 p = vec2(v_wpos.x, v_wpos.z);
		float slon = p.x * sH + p.y * cH;   // along the hull (fwd = sin,cos)
		float slat = p.x * cH - p.y * sH;   // across the hull
		float spd = u_shipDyn.x;
		float shipFoam = 0.0;

		// Waterline foam ring hugging the hull edge.
		float e = (slat / hb) * (slat / hb) + (slon / hl) * (slon / hl);
		float ring = (1.0 - smoothstep(0.0, 0.5, abs(e - 1.0))) * smoothstep(2.4, 1.0, e);
		shipFoam += ring * 0.75;

		// Bow wave: foam pushed ahead of the stem, narrowing to the centreline.
		float bowT = smoothstep(hl * 1.8, hl * 0.7, slon) * step(hl * 0.35, slon);
		shipFoam += bowT * smoothstep(hb * 1.4, 0.0, abs(slat)) * spd * 0.9;

		// Wake: a spreading V of foam trailing the stern, fading with distance.
		float wd = -slon - hl * 0.5;
		if (wd > 0.0)
		{
			float arm = hb * 0.6 + wd * 0.34;
			float vfoam = exp(-pow((abs(slat) - arm) / (hb * 0.55), 2.0));
			shipFoam += vfoam * exp(-wd * 0.02) * spd * 0.8;
			shipFoam += smoothstep(hb * 0.9, 0.0, abs(slat)) * exp(-wd * 0.06) * spd * 0.5; // churn behind the stern
		}

		col = mix(col, vec3(0.93, 0.96, 1.0), clamp(shipFoam, 0.0, 1.0));
	}

	// Distance fog: the sea dissolves into the horizon sky colour, killing the
	// hard sea/sky seam and adding aerial depth.
	float dist = length(toEye);
	float fog = smoothstep(u_fog.w * 0.5, u_fog.w, dist);
	col = mix(col, u_fog.xyz, fog);

	gl_FragColor = vec4(col, 1.0);
}
