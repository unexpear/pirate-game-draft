$input a_position
$output v_uv

// Sky vertex shader: a_position is a fullscreen triangle already in clip space
// (xy in [-1,3]). Draw it at the far plane; the gradient is done in the FS.
#include <bgfx_shader.sh>

void main()
{
	gl_Position = vec4(a_position.xy, 0.999999, 1.0);
	v_uv = a_position.xy * 0.5 + 0.5; // 0 at bottom(horizon) .. 1 at top(zenith)
}
