#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct vs_params
{
    float4x4 projection;
    float4 bones[192];
};

struct main0_out
{
    float2 v_uv [[user(locn0)]];
    float4 v_light_color [[user(locn1)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float2 a_local_position0 [[attribute(0)]];
    float2 a_local_position1 [[attribute(1)]];
    float2 a_local_position2 [[attribute(2)]];
    float2 a_local_position3 [[attribute(3)]];
    float2 a_uv [[attribute(4)]];
    float4 a_bone_indices [[attribute(5)]];
    float4 a_bone_weights [[attribute(6)]];
    float4 a_light_color [[attribute(7)]];
};

static inline __attribute__((always_inline))
float2 marrow_transform_point(thread const float2& local_position, thread const int& bone_index, constant vs_params& _33)
{
    int _23 = (bone_index / 2) * 3;
    int _43 = _23 + 1;
    int _48 = _23 + 2;
    float4 matrix_values;
    float2 translation;
    if ((bone_index & 1) == 0)
    {
        matrix_values = _33.bones[_23];
        translation = _33.bones[_43].xy;
    }
    else
    {
        matrix_values = float4(_33.bones[_43].zw, _33.bones[_48].xy);
        translation = _33.bones[_48].zw;
    }
    return float2(((matrix_values.x * local_position.x) + (matrix_values.z * local_position.y)) + translation.x, ((matrix_values.y * local_position.x) + (matrix_values.w * local_position.y)) + translation.y);
}

vertex main0_out main0(main0_in in [[stage_in]], constant vs_params& _33 [[buffer(0)]])
{
    main0_out out = {};
    float2 param = in.a_local_position0;
    int param_1 = int(in.a_bone_indices.x);
    float2 param_2 = in.a_local_position1;
    int param_3 = int(in.a_bone_indices.y);
    float2 param_4 = in.a_local_position2;
    int param_5 = int(in.a_bone_indices.z);
    float2 param_6 = in.a_local_position3;
    int param_7 = int(in.a_bone_indices.w);
    out.v_uv = in.a_uv;
    out.v_light_color = in.a_light_color;
    out.gl_Position = _33.projection * float4((((marrow_transform_point(param, param_1, _33) * in.a_bone_weights.x) + (marrow_transform_point(param_2, param_3, _33) * in.a_bone_weights.y)) + (marrow_transform_point(param_4, param_5, _33) * in.a_bone_weights.z)) + (marrow_transform_point(param_6, param_7, _33) * in.a_bone_weights.w), 0.0, 1.0);
    return out;
}

