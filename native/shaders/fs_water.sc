$input v_normal, v_wpos, v_worldxz

// Water fragment shader: depth-shaded blue with a fresnel rim and foam on the
// crests. The sea is CUT OUT where land is (u_landCut), with foam at the shore.
#include <bgfx_shader.sh>

uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_camPos;   // xyz = eye position
uniform vec4 u_landCut;  // xy = land centre (world), z = radius, w unused
uniform vec4 u_fog;      // xyz = horizon fog colour, w = fog far distance
uniform vec4 u_waveTime; // x = time (same uniform the vertex shader displaces with)
uniform vec4 u_ship[8];    // per hull: xy = heading (sin,cos), z = half-length, w = half-beam
uniform vec4 u_shipDyn[8]; // per hull: x = speed 0..1, yz = scene XZ (the player is 0,0)
                           // 8 == sea::kMaxWakeHulls. Empty slots have half-length 0.
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
	float cur = ndc.z - 0.0006;
	float texel = 1.0 / 2048.0;
	float sh = 0.0;
	for (int y = -1; y <= 1; ++y)
	for (int x = -1; x <= 1; ++x)
		sh += (cur > texture2DLod(s_shadowMap, uv + vec2(float(x), float(y)) * texel, 0.0).x) ? 1.0 : 0.0;
	return 1.0 - (sh / 9.0) * 0.40;
}

// One hull's foam. p is scene XZ relative to the hull (forward = (sin, cos)).
// Square the gaussian by multiply — pow(negative, 2) is NaN and ate the wake
// between the arms. Scroll term matches wake.cpp (u_waveTime.x is timeSec).
float hullFoam(vec2 p, float sH, float cH, float hl, float hb, float spd, float timeSec)
{
	float slon = p.x * sH + p.y * cH; // along the hull
	float slat = p.x * cH - p.y * sH; // across the hull
	float foam = 0.0;

	float e = (slat / hb) * (slat / hb) + (slon / hl) * (slon / hl);
	float ring = (1.0 - smoothstep(0.0, 0.5, abs(e - 1.0))) * smoothstep(2.4, 1.0, e);
	foam += ring * 0.75;

	float bowT = smoothstep(hl * 1.8, hl * 0.7, slon) * step(hl * 0.35, slon);
	foam += bowT * smoothstep(hb * 1.4, 0.0, abs(slat)) * spd * 0.9;

	float wd = -slon - hl * 0.5;
	if (wd > 0.0)
	{
		float arm = hb * 0.6 + wd * 0.34;
		float d = (abs(slat) - arm) / (hb * 0.55);
		float vfoam = exp(-(d * d));
		float scroll = 0.75 + 0.25 * sin(wd * 0.35 - timeSec * 1.7);
		foam += vfoam * exp(-wd * 0.02) * spd * 0.8 * scroll;
		foam += smoothstep(hb * 0.9, 0.0, abs(slat)) * exp(-wd * 0.06) * spd * 0.5; // churn behind the stern
	}
	return foam;
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

	// --- Hulls in the sea. Same pattern for every slot (player at the origin,
	// the enemy, moored boats): a foam ring at the waterline, a bow wave, and a
	// trailing V-wake. Must match sea::hullFoam in wake.cpp.
	// p is scene XZ relative to THAT hull, so a ship away from the origin still
	// foams around itself. Reach matches wakeReach() in wake.cpp.
	float shipFoam = 0.0;
	for (int i = 0; i < 8; ++i)
	{
		float hl = u_ship[i].z;
		float hb = u_ship[i].w;
		if (hl >= 0.1 && hb >= 0.05)
		{
			float spd = u_shipDyn[i].x;
			vec2 p = vec2(v_wpos.x - u_shipDyn[i].y, v_wpos.z - u_shipDyn[i].z);
			float reach = (spd > 0.01) ? (hl * 2.0 + 160.0) : (hl * 2.5 + 6.0);
			if (dot(p, p) <= reach * reach)
				shipFoam += hullFoam(p, u_ship[i].x, u_ship[i].y, hl, hb, spd, u_waveTime.x);
		}
	}
	col = mix(col, vec3(0.93, 0.96, 1.0), clamp(shipFoam, 0.0, 1.0));

	// Distance fog: the sea dissolves into the horizon sky colour, killing the
	// hard sea/sky seam and adding aerial depth.
	float dist = length(toEye);
	float fog = smoothstep(u_fog.w * 0.5, u_fog.w, dist);
	col = mix(col, u_fog.xyz, fog);

	gl_FragColor = vec4(col, 1.0);
}
