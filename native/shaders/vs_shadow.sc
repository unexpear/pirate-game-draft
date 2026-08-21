$input a_position

// Shadow depth pass: just transform by the light's model-view-proj. The
// fragment shader writes the depth; casters are submitted with this program to
// the shadow view (whose camera is the sun's orthographic view).
#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
}
