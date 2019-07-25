// Copyright (c) Facebook, Inc. and its affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include <Magnum/Magnum.h>
#include <gl_tensor_param.h>

#include "esp/core/esp.h"

#include "WindowlessContext.h"

namespace esp {
namespace gfx {

class RenderingTarget {
 public:
  RenderingTarget(WindowlessContext::ptr context, const Magnum::Vector2i& size);

  ~RenderingTarget() { LOG(INFO) << "Deconstructing RenderingTarget"; }

  void renderEnter();
  void renderExit();

  gltensor::GLTensorParam::ptr glTensorParamRgba() const;
  gltensor::GLTensorParam::ptr glTensorParamDepth() const;
  gltensor::GLTensorParam::ptr glTensorParamId() const;

  void readFrameRgba(const Magnum::MutableImageView2D& view);
  void readFrameDepth(const Magnum::MutableImageView2D& view);
  void readFrameObjectId(const Magnum::MutableImageView2D& view);

  Magnum::Vector2i framebufferSize() const;

  ESP_SMART_POINTERS_WITH_UNIQUE_PIMPL(RenderingTarget);
};

}  // namespace gfx
}  // namespace esp
