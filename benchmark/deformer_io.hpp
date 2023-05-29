#pragma once

#include "deformer.hpp"

#include <asioext/open.hpp>

#include <boost/asio/read.hpp>

#include <vector>

std::vector<deformanble_mesh> load_meshes(const char* filename)
{
  namespace net = boost::asio;

  std::vector<deformanble_mesh> meshes;
  auto fh = asioext::open(filename, asioext::open_flags::access_read | asioext::open_flags::open_existing);

  uint32_t num_meshes, num_vertices, num_bone_transforms;
  net::read(fh, net::buffer(&num_meshes, sizeof(num_meshes)));
  meshes.resize(num_meshes);

  for (uint32_t i = 0; i != num_meshes; ++i) {
    // Read vertices
    net::read(fh, net::buffer(&num_vertices, sizeof(num_vertices)));
    meshes[i].vertices.resize(num_vertices);
    for (uint32_t j = 0; j != num_vertices; ++j) {
      auto& vertex = meshes[i].vertices[j];
      net::read(fh, net::buffer(&vertex.position, sizeof(vertex.position)));
      net::read(fh, net::buffer(vertex.bone_weights, sizeof(vertex.bone_weights)));
      net::read(fh, net::buffer(vertex.bone_indices, sizeof(vertex.bone_indices)));
      net::read(fh, net::buffer(&vertex.normal, sizeof(vertex.normal)));
      net::read(fh, net::buffer(vertex.uv, sizeof(vertex.uv)));
    }

    // Read bone transform matrices
    net::read(fh, net::buffer(&num_bone_transforms, sizeof(num_bone_transforms)));
    meshes[i].bone_transforms.resize(num_bone_transforms);
    for (uint32_t j = 0; j != num_bone_transforms; ++j) {
      auto& transform = meshes[i].bone_transforms[j];
      net::read(fh, net::buffer(transform.m, sizeof(transform.m)));
    }
  }

  // Quick way to increase data size - baseline is 25MiB+, so we don't worry about CPU caches etc.
  //for (int i = 0; i < 2; ++i)
  //  meshes.insert(meshes.end(), meshes.begin(), meshes.end());

  return meshes;
}
