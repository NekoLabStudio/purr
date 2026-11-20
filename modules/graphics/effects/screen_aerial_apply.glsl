#[compute]

#version 450

#VERSION_DEFINES

// The air laid over what the camera sees (AerialPerspectiveEffect):
// each pixel's colour dimmed by the air between it and the camera and the
// air's own light added, read from the view's volume (screen_aerial_volume.glsl)
// at the pixel's distance. The sky (the depth buffer's far plane) has its
// air already.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D colour_image;
layout(set = 0, binding = 1) uniform sampler2D depth_texture;
layout(set = 0, binding = 2) uniform sampler3D volume;

layout(push_constant, std430) uniform Params {
	mat4 inverse_projection;  // the view's, as Godot's depth reads (reversed, depth-corrected)
	vec4 size;  // xy the picture in pixels, z km to the volume's last slice, w its slices
} params;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(params.size.x) || pixel.y >= int(params.size.y)) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / params.size.xy;
	float depth = textureLod(depth_texture, uv, 0.0).r;
	if (depth <= 0.0) {
		return;  // the sky
	}
	vec4 view = params.inverse_projection * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
	float metres = length(view.xyz / (abs(view.w) > 1e-8 ? view.w : 1e-8));
	float slices = params.size.w;
	// Slice k reaches (k + 1) / slices of the way: nearer than the first, its air is scaled down to none at the camera.
	float slice = metres / 1000.0 / max(params.size.z, 1e-3) * slices - 1.0;
	float nearer = clamp(slice + 1.0, 0.0, 1.0);
	vec4 air = textureLod(volume, vec3(uv, (clamp(slice, 0.0, slices - 1.0) + 0.5) / slices), 0.0);
	vec3 light = air.rgb * nearer;
	float transmittance = mix(1.0, air.a, nearer);
	vec4 colour = imageLoad(colour_image, pixel);
	imageStore(colour_image, pixel, vec4(colour.rgb * transmittance + light, colour.a));
}
