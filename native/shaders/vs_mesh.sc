$input a_position, a_normal
$output v_normal

// Lit-mesh vertex shader: transform position by the model-view-proj, and the
// normal by the model matrix (good enough for the ship's box pieces).
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
	v_normal = normalize(mul(u_model[0], vec4(a_normal, 0.0) ).xyz);
}
