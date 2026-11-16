# Purr's build profile, used for every editor and export template.
# Pass it to SCons from the godot/ folder: scons profile=../build/purr.py

opengl3 = "no"  # Compatibility renderer off; Vulkan, D3D12 and Metal stay
deprecated = "no"  # Leave out compatibility code for deprecated and removed features
module_text_server_fb_enabled = "no"  # Keep the advanced text server (Japanese line breaking)
module_mobile_vr_enabled = "no"  # Cardboard-style phone VR; not used
module_webxr_enabled = "no"
module_godot_physics_3d_enabled = "no"  # 3D projects use Jolt; Godot Physics 2D stays
