$input v_normal, v_wpos

// Lit-mesh fragment shader: a warm sun gated by the shadow map, plus a
// hemispheric sky/ground ambient, tinted by u_color, with a procedural surface
// texture (u_mat): plank courses for timber, block courses for stone/masonry.
// Keeps the flat-shaded look but breaks up the solid colours.
#include <bgfx_shader.sh>

uniform vec4 u_color;    // rgb = piece colour
uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_mat;      // x: 0 = flat, 1 = timber planks, 2 = stone courses, 3 = canvas sail
uniform vec4 u_camPos;   // xyz = eye position (for distance fog)
uniform vec4 u_fog;      // xyz = horizon fog colour, w = fog far distance
uniform mat4 u_lightMtx; // light view-proj, for shadow lookup
SAMPLER2D(s_shadowMap, 4);

// How much of the SUN reaches this point (1 = full sun, 0 = fully shadowed),
// from the shadow map with a 3x3 PCF. Shadowing removes only the sun term — the
// sky still fills a shadow — so shade reads as real shade, not a grey wash.
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

// Soft highlight shoulder: everything below 0.82 is left exactly as-is (so sky,
// fog and shade match), and brighter values roll smoothly toward 1 instead of
// clipping sunlit whitewash to a flat, detail-less white.
vec3 shoulder(vec3 c) {
	vec3 over = max(c - vec3_splat(0.82), vec3_splat(0.0));
	return min(c, vec3_splat(0.82)) + vec3_splat(0.18) * (vec3_splat(1.0) - exp(-over / 0.18));
}

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
	float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main()
{
	vec3 N = normalize(v_normal);
	vec3 L = normalize(u_lightDir.xyz);
	float ndl = max(dot(N, L), 0.0);
	vec3 base = u_color.xyz;

	if (u_mat.x > 0.5 && u_mat.x < 2.5)
	{
		// Courses run horizontally on walls/hull; on near-flat faces (decks,
		// roofs, ground) they run along world X instead so they still read.
		float up = abs(N.y);
		float coord = mix(v_wpos.y, v_wpos.x, step(0.6, up));
		float plankH = (u_mat.x > 1.5) ? 1.6 : 0.85; // stone courses taller than planks
		float f = fract(coord / plankH);
		// Dark seam line between courses.
		float seam = smoothstep(0.0, 0.07, f) * smoothstep(0.0, 0.07, 1.0 - f);
		base *= mix(0.70, 1.0, seam);

		if (u_mat.x > 1.5)
		{
			// Stone: stagger vertical joints every other course.
			float row = floor(coord / plankH);
			float along = mix(v_wpos.x, v_wpos.z, step(0.6, up));
			float jointPhase = along / 3.0 + 0.5 * mod(row, 2.0);
			float jf = fract(jointPhase);
			float vjoint = smoothstep(0.0, 0.06, jf) * smoothstep(0.0, 0.06, 1.0 - jf);
			base *= mix(0.74, 1.0, vjoint);
			base *= 0.94 + 0.12 * vnoise(v_wpos.xz * 1.3 + row); // block-to-block tone
		}
		else
		{
			// Timber: lengthwise grain streaks.
			float along = mix(v_wpos.z, v_wpos.z, step(0.6, up));
			float grain = vnoise(vec2(along * 4.0, coord * 1.5));
			base *= 0.90 + 0.16 * grain;
		}
	}

	vec3 col;
	if (u_mat.x > 2.5)
	{
		// Canvas sail: bright warm cloth that stays creamy in shade (no cool fill).
		col = base * (0.74 + 0.32 * ndl);
	}
	else
	{
		// Warm afternoon sun, gated by the shadow map (sampled with a small normal
		// offset so lit faces don't self-shadow), plus a HEMISPHERIC ambient: cool
		// sky light from above, warm bounce off the sand and stone from below — so
		// roofs, walls and eaves each get a different fill and read as form.
		float vis = sunVisibility(v_wpos + N * 0.30);
		vec3 sun = vec3(1.30, 1.16, 0.94);
		vec3 sky = vec3(0.52, 0.64, 0.82);
		vec3 gnd = vec3(0.50, 0.44, 0.35);
		vec3 amb = mix(gnd, sky, N.y * 0.5 + 0.5) * 0.90;
		col = base * (amb + sun * (ndl * vis * 0.95));
	}
	col = shoulder(col);

	// Aerial-perspective fog: distant structures/land recede into the horizon
	// colour (matches the water + sky), so depth reads and nothing sits flat.
	if (u_fog.w > 1.0)
	{
		float dist = length(u_camPos.xyz - v_wpos);
		float fog = smoothstep(u_fog.w * 0.32, u_fog.w, dist); // land recedes into haze earlier than the sea
		col = mix(col, u_fog.xyz, fog);
	}

	gl_FragColor = vec4(col, u_color.w); // w<1 for translucent contact shadows
}
