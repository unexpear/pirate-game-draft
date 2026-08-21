$input v_normal, v_wpos, v_worldxz

// Water fragment shader: depth-shaded blue with a fresnel rim and foam on the
// crests. The sea is CUT OUT where land is (u_landCut), with foam at the shore.
#include <bgfx_shader.sh>

uniform vec4 u_lightDir; // xyz = direction to the sun
uniform vec4 u_camPos;   // xyz = eye position
uniform vec4 u_landCut;  // xy = land centre (world), z = radius, w unused
uniform vec4 u_fog;      // xyz = horizon fog colour, w = fog far distance

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
	float foam = smoothstep(0.16, 0.34, slope) * smoothstep(0.15, 0.55, v_wpos.y);
	col = mix(col, vec3(0.88, 0.93, 1.0), foam * 0.75);

	// Foam ring where the sea meets the shore.
	if (u_landCut.z > 0.5)
	{
		float shore = 1.0 - smoothstep(u_landCut.z, u_landCut.z + 4.0, landD);
		col = mix(col, vec3(0.82, 0.90, 0.98), shore * 0.65);
	}

	// Distance fog: the sea dissolves into the horizon sky colour, killing the
	// hard sea/sky seam and adding aerial depth.
	float dist = length(toEye);
	float fog = smoothstep(u_fog.w * 0.5, u_fog.w, dist);
	col = mix(col, u_fog.xyz, fog);

	gl_FragColor = vec4(col, 1.0);
}
