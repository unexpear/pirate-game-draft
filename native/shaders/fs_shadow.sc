// Shadow depth pass: write the light-space depth into an R32F target so the
// main pass can compare against it.
#include <bgfx_shader.sh>

void main()
{
	gl_FragColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
