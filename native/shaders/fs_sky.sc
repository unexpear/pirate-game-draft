$input v_uv

// Sky fragment shader: a vertical gradient from a warm pale haze at the horizon
// to a deep navy zenith, so the top third of the frame reads as sky/atmosphere
// instead of a dead flat band. The horizon colour is shared with the water's
// distance fog so the sea dissolves into the sky (no hard seam).
#include <bgfx_shader.sh>

uniform vec4 u_skyTop;     // zenith rgb
uniform vec4 u_skyHorizon; // horizon haze rgb

void main()
{
	// The horizon sits high in frame (chase/orbit cams look slightly down), so
	// keep the warm haze band up near where sky meets sea, navy above it.
	float t = smoothstep(0.52, 0.92, v_uv.y); // warm horizon .. navy zenith
	vec3 col = mix(u_skyHorizon.xyz, u_skyTop.xyz, t);
	gl_FragColor = vec4(col, 1.0);
}
