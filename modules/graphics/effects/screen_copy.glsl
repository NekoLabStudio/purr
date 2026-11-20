#[compute]

#version 450

#VERSION_DEFINES

// The colour as drawn, copied for effects that read pixels other than their own while writing.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict readonly image2D source_image;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D copy_image;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pixel, imageSize(source_image)))) {
		return;
	}
	imageStore(copy_image, pixel, imageLoad(source_image, pixel));
}
