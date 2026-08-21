$input a_position, a_normal, a_color0
$output v_normal, v_color, v_wpos

// Terrain vertex shader: a per-vertex-coloured lit heightfield (the island).
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0) ).xyz);
	v_color = a_color0;
	v_wpos = mul(u_model[0], vec4(a_position, 1.0) ).xyz; // for distance fog
}
