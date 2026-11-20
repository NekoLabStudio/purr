#pragma once

// Depth of field round a focus distance, as VirtualCamera3D's autofocus and PhotoMode set it:
// sharp just either side of it, blurring further away, and close up nearer than it.

#include "scene/resources/camera_attributes.h"

namespace CameraFocus {

inline void set(CameraAttributesPractical *p_attributes, float p_distance, float p_blur) {
	p_attributes->set_dof_blur_far_enabled(p_blur > 0.0f);
	p_attributes->set_dof_blur_far_distance(p_distance * 1.2f);
	p_attributes->set_dof_blur_far_transition(p_distance);
	p_attributes->set_dof_blur_near_enabled(p_blur > 0.0f && p_distance > 2.0f);
	p_attributes->set_dof_blur_near_distance(p_distance * 0.6f);
	p_attributes->set_dof_blur_near_transition(p_distance * 0.4f);
	p_attributes->set_dof_blur_amount(p_blur * 0.25f);
}

} // namespace CameraFocus
