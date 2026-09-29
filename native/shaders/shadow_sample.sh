// Shared shadow-map lookup for mesh, terrain and water receivers.
// Include after bgfx_shader.sh. Two cascades, 3x3 PCF, same texel on both
// (both maps are kCascadeResolution). Cascade 0 wins when the fragment is
// inside it; otherwise cascade 1, if u_shadowParams.x is set.
//
// u_shadowParams: x = cascade 1 enabled (0/1), y = texel in UV (1/size),
//                 z = depth bias subtracted from the receiver.

uniform mat4 u_lightMtx0;
uniform mat4 u_lightMtx1;
uniform vec4 u_shadowParams;
SAMPLER2D(s_shadowMap, 4);
SAMPLER2D(s_shadowMapFar, 5);

// x = sun visibility (1 = full sun), y = 1 when the point is inside the map.
vec2 shadowCascade0(vec3 wpos) {
	vec4 lc = mul(u_lightMtx0, vec4(wpos, 1.0));
	vec3 ndc = lc.xyz / lc.w;
	vec2 uv = ndc.xy * 0.5 + 0.5;
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_PSSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV
	uv.y = 1.0 - uv.y;
#endif
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec2(1.0, 0.0);
	float cur = ndc.z - u_shadowParams.z;
	float texel = u_shadowParams.y;
	float sh = 0.0;
	for (int y = -1; y <= 1; ++y)
	for (int x = -1; x <= 1; ++x)
		sh += (cur > texture2DLod(s_shadowMap, uv + vec2(float(x), float(y)) * texel, 0.0).x) ? 1.0 : 0.0;
	return vec2(1.0 - sh / 9.0, 1.0);
}

vec2 shadowCascade1(vec3 wpos) {
	vec4 lc = mul(u_lightMtx1, vec4(wpos, 1.0));
	vec3 ndc = lc.xyz / lc.w;
	vec2 uv = ndc.xy * 0.5 + 0.5;
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_PSSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV
	uv.y = 1.0 - uv.y;
#endif
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec2(1.0, 0.0);
	float cur = ndc.z - u_shadowParams.z;
	float texel = u_shadowParams.y;
	float sh = 0.0;
	for (int y = -1; y <= 1; ++y)
	for (int x = -1; x <= 1; ++x)
		sh += (cur > texture2DLod(s_shadowMapFar, uv + vec2(float(x), float(y)) * texel, 0.0).x) ? 1.0 : 0.0;
	return vec2(1.0 - sh / 9.0, 1.0);
}

// 1 = full sun, 0 = fully shadowed. Outside both maps the sun is unoccluded.
float shadowVisibility(vec3 wpos) {
	vec2 nearMap = shadowCascade0(wpos);
	if (nearMap.y > 0.5) return nearMap.x;
	if (u_shadowParams.x > 0.5) {
		vec2 farMap = shadowCascade1(wpos);
		if (farMap.y > 0.5) return farMap.x;
	}
	return 1.0;
}
