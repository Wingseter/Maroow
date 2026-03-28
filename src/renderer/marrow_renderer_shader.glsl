@module marrow_renderer

@vs single_color_vs
layout(binding=0) uniform vs_params {
    mat4 projection;
    vec4 bones[192];
};

layout(location=0) in vec2 a_local_position0;
layout(location=1) in vec2 a_local_position1;
layout(location=2) in vec2 a_local_position2;
layout(location=3) in vec2 a_local_position3;
layout(location=4) in vec2 a_uv;
layout(location=5) in vec4 a_bone_indices;
layout(location=6) in vec4 a_bone_weights;
layout(location=7) in vec4 a_light_color;

layout(location=0) out vec2 v_uv;
layout(location=1) out vec4 v_light_color;

vec2 marrow_transform_point(vec2 local_position, int bone_index) {
    int pair_index = bone_index / 2;
    int base_index = pair_index * 3;
    vec4 packed0 = bones[base_index + 0];
    vec4 packed1 = bones[base_index + 1];
    vec4 packed2 = bones[base_index + 2];

    vec4 matrix_values;
    vec2 translation;
    if ((bone_index & 1) == 0) {
        matrix_values = packed0;
        translation = packed1.xy;
    } else {
        matrix_values = vec4(packed1.zw, packed2.xy);
        translation = packed2.zw;
    }

    return vec2(
        (matrix_values.x * local_position.x) + (matrix_values.z * local_position.y) + translation.x,
        (matrix_values.y * local_position.x) + (matrix_values.w * local_position.y) + translation.y);
}

void main() {
    vec2 world_position =
        marrow_transform_point(a_local_position0, int(a_bone_indices.x)) * a_bone_weights.x +
        marrow_transform_point(a_local_position1, int(a_bone_indices.y)) * a_bone_weights.y +
        marrow_transform_point(a_local_position2, int(a_bone_indices.z)) * a_bone_weights.z +
        marrow_transform_point(a_local_position3, int(a_bone_indices.w)) * a_bone_weights.w;
    v_uv = a_uv;
    v_light_color = a_light_color;
    gl_Position = projection * vec4(world_position, 0.0, 1.0);
}
@end

@fs single_color_fs
layout(binding=1) uniform fs_params {
    vec4 pma_control;
};

layout(binding=0) uniform texture2D atlas_texture;
layout(binding=0) uniform sampler atlas_sampler;

layout(location=0) in vec2 v_uv;
layout(location=1) in vec4 v_light_color;
layout(location=0) out vec4 frag_color;

void main() {
    vec4 tex_color = texture(sampler2D(atlas_texture, atlas_sampler), v_uv);
    vec4 output_color = tex_color * v_light_color;
    if (pma_control.x < 0.5) {
        output_color.rgb *= v_light_color.a;
    }
    frag_color = output_color;
}
@end

@program single_color single_color_vs single_color_fs

@vs two_color_vs
layout(binding=0) uniform vs_params {
    mat4 projection;
    vec4 bones[192];
};

layout(location=0) in vec2 a_local_position0;
layout(location=1) in vec2 a_local_position1;
layout(location=2) in vec2 a_local_position2;
layout(location=3) in vec2 a_local_position3;
layout(location=4) in vec2 a_uv;
layout(location=5) in vec4 a_bone_indices;
layout(location=6) in vec4 a_bone_weights;
layout(location=7) in vec4 a_light_color;
layout(location=8) in vec4 a_dark_color;

layout(location=0) out vec2 v_uv;
layout(location=1) out vec4 v_light_color;
layout(location=2) out vec4 v_dark_color;

vec2 marrow_transform_point(vec2 local_position, int bone_index) {
    int pair_index = bone_index / 2;
    int base_index = pair_index * 3;
    vec4 packed0 = bones[base_index + 0];
    vec4 packed1 = bones[base_index + 1];
    vec4 packed2 = bones[base_index + 2];

    vec4 matrix_values;
    vec2 translation;
    if ((bone_index & 1) == 0) {
        matrix_values = packed0;
        translation = packed1.xy;
    } else {
        matrix_values = vec4(packed1.zw, packed2.xy);
        translation = packed2.zw;
    }

    return vec2(
        (matrix_values.x * local_position.x) + (matrix_values.z * local_position.y) + translation.x,
        (matrix_values.y * local_position.x) + (matrix_values.w * local_position.y) + translation.y);
}

void main() {
    vec2 world_position =
        marrow_transform_point(a_local_position0, int(a_bone_indices.x)) * a_bone_weights.x +
        marrow_transform_point(a_local_position1, int(a_bone_indices.y)) * a_bone_weights.y +
        marrow_transform_point(a_local_position2, int(a_bone_indices.z)) * a_bone_weights.z +
        marrow_transform_point(a_local_position3, int(a_bone_indices.w)) * a_bone_weights.w;
    v_uv = a_uv;
    v_light_color = a_light_color;
    v_dark_color = a_dark_color;
    gl_Position = projection * vec4(world_position, 0.0, 1.0);
}
@end

@fs two_color_fs
layout(binding=1) uniform fs_params {
    vec4 pma_control;
};

layout(binding=0) uniform texture2D atlas_texture;
layout(binding=0) uniform sampler atlas_sampler;

layout(location=0) in vec2 v_uv;
layout(location=1) in vec4 v_light_color;
layout(location=2) in vec4 v_dark_color;
layout(location=0) out vec4 frag_color;

void main() {
    vec4 tex_color = texture(sampler2D(atlas_texture, atlas_sampler), v_uv);
    vec3 tint_rgb =
        ((pma_control.x + ((1.0 - pma_control.x) * tex_color.a)) - tex_color.rgb) *
            v_dark_color.rgb +
        (tex_color.rgb * v_light_color.rgb);
    frag_color = vec4(tint_rgb, tex_color.a * v_light_color.a);
}
@end

@program two_color two_color_vs two_color_fs
