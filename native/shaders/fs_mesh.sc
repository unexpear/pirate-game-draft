$input v_normal, v_wpos

// Lit-mesh fragment shader: directional N.L + ambient, tinted by u_color, with a
// procedural surface texture (u_mat): plank courses for timber, block courses
// for stone/masonry. Keeps the flat-shaded look but breaks up the solid colours.
#include <bgfx_shader.sh>

uniform vec4 u_color;    // rgb = piece colour
uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_mat;      // x: 0 = flat, 1 = timber planks, 2 = stone courses

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
	float ambient = 0.35;
	vec3 base = u_color.xyz;

	if (u_mat.x > 0.5)
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

	vec3 col = base * (ambient + (1.0 - ambient) * ndl);
	gl_FragColor = vec4(col, 1.0);
}
