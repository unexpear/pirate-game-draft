$input a_position
$output v_normal, v_wpos

// Water vertex shader: displaces a flat grid by the same multi-frequency sine
// sum the CPU model uses (sea::sampleWater), and derives the analytic normal.
// Wave params come in as uniforms so the GPU surface matches the buoyancy math.
#include <bgfx_shader.sh>

uniform vec4 u_waveA[3]; // xy = direction (cos,sin), z = amplitude, w = wavelength
uniform vec4 u_waveB[3]; // x = speed, y = phase
uniform vec4 u_waveTime; // x = time

void main()
{
	vec3 p = a_position;
	float height = 0.0;
	float dhdx = 0.0;
	float dhdz = 0.0;

	for (int i = 0; i < 3; ++i)
	{
		vec2  dir        = u_waveA[i].xy;
		float amp        = u_waveA[i].z;
		float wavelength = u_waveA[i].w;
		float speed      = u_waveB[i].x;
		float phase      = u_waveB[i].y;

		float k = 6.2831853 / wavelength;
		float f = k * (dir.x * p.x + dir.y * p.z) + phase + u_waveTime.x * speed;
		height += amp * sin(f);
		float c = amp * k * cos(f);
		dhdx += c * dir.x;
		dhdz += c * dir.y;
	}

	p.y = height;
	v_normal = normalize(vec3(-dhdx, 1.0, -dhdz) );
	v_wpos = p;
	gl_Position = mul(u_modelViewProj, vec4(p, 1.0) );
}
