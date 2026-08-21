$input a_position
$output v_normal, v_wpos, v_worldxz

// Water vertex shader: a sum of GERSTNER wave trains. Gerstner adds a horizontal
// crest-pinch on top of the vertical motion, so crests sharpen and troughs
// flatten (directional swell) instead of the rounded sine bumps that read as a
// tiled grid. Six non-harmonic trains (params from the CPU wave field as
// uniforms). The analytic normal is derived from the summed wave derivatives.
#include <bgfx_shader.sh>

uniform vec4 u_waveA[6];  // xy = direction (cos,sin), z = amplitude, w = wavelength
uniform vec4 u_waveB[6];  // x = speed, y = phase
uniform vec4 u_waveTime;  // x = time
uniform vec4 u_waveOffset; // xy = ship virtual world position (grid stays put, ocean scrolls)

void main()
{
	vec3 p = a_position;
	float wx = p.x + u_waveOffset.x;
	float wz = p.z + u_waveOffset.y;

	const float steepness = 0.75; // total crest sharpness (< 1 avoids looping crests)
	const float invN = 1.0 / 6.0;

	float height = 0.0;
	float gx = 0.0, gz = 0.0;              // horizontal Gerstner displacement
	float nx = 0.0, ny = 1.0, nz = 0.0;    // accumulated analytic normal

	for (int i = 0; i < 6; ++i)
	{
		vec2  dir        = u_waveA[i].xy;
		float amp        = u_waveA[i].z;
		float wavelength = u_waveA[i].w;
		float speed      = u_waveB[i].x;
		float phase      = u_waveB[i].y;

		float k = 6.2831853 / wavelength;
		float f = k * (dir.x * wx + dir.y * wz) + phase + u_waveTime.x * speed;
		float S = sin(f);
		float C = cos(f);
		// Per-wave steepness Q, scaled so the summed steepness == `steepness`.
		float Q = steepness / (k * amp * 6.0);

		height += amp * S;
		gx += Q * amp * dir.x * C;
		gz += Q * amp * dir.y * C;

		float WA = k * amp;
		nx -= dir.x * WA * C;
		nz -= dir.y * WA * C;
		ny -= Q * WA * S;
	}

	p.x += gx; // Gerstner pinch pulls the surface toward the crests
	p.z += gz;
	p.y = height;

	v_normal = normalize(vec3(nx, ny, nz));
	v_wpos = p;
	v_worldxz = vec3(wx, wz, 0.0); // undisplaced world XZ, for the land cut
	gl_Position = mul(u_modelViewProj, vec4(p, 1.0) );
}
