#pragma once

#define _XM_NO_INTRINSICS_ 1
#include "SimpleMath.h"

// Grab types we need
using namespace DirectX;
using float4x4 = DirectX::SimpleMath::Matrix;
using float3 = DirectX::SimpleMath::Vector3;

struct pwnt3432_vertex
{
  float3 position;
  uint8_t bone_weights[4];
  uint8_t bone_indices[4];
  float3 normal;
  float uv[2]; // texture coordinates ignored in this benchmark
};

// TODO: enforce data alignment, not too relevant for benchmark tho
struct deformanble_mesh
{
  std::vector<pwnt3432_vertex> vertices;
  std::vector<float4x4> bone_transforms;
};

template <std::size_t N>
inline XMMATRIX calculate_summed_matrix(const uint8_t (&indices)[N], const uint8_t (&weights)[N],
                                        const float4x4* bone_transforms)
{
  static_assert(N >= 1);
  constexpr float kOneOver255 = 1.0f / 255.f;

  // Initialize with first weighted transform
  XMMATRIX summed_matrix;
  {
    const XMMATRIX transform = bone_transforms[indices[0]];
    const float w = weights[0] * kOneOver255;
    summed_matrix = transform * w;
  }

  // Sum up the remaining
  for (std::size_t i = 1; i != N; ++i) {
    const XMMATRIX transform = bone_transforms[indices[i]];
    const float w = weights[i] * kOneOver255;
    summed_matrix += transform * w;
  }
  return summed_matrix;
}

template <typename InputVertexType, typename OutputVertexType>
void deform_vertices(const InputVertexType* input_vertices, OutputVertexType* output_vertices,
                     const float4x4* bone_transforms, uint32_t num_vertices)
{
  static_assert(sizeof(input_vertices->bone_weights) / sizeof(input_vertices->bone_weights[0]) ==
                sizeof(input_vertices->bone_indices) / sizeof(input_vertices->bone_indices[0]),
                "Same number of weights & indices");

  for (uint32_t i = 0; i != num_vertices; ++i) {
    const auto& vertex = input_vertices[i];
    XMMATRIX summed_matrix = calculate_summed_matrix(vertex.bone_indices, vertex.bone_weights, bone_transforms);
    output_vertices[i].position = XMVector3TransformCoord(vertex.position, summed_matrix);
    output_vertices[i].normal = XMVector3TransformNormal(vertex.normal, summed_matrix);
  }
}
