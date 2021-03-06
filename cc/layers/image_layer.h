// Copyright 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYERS_IMAGE_LAYER_H_
#define CC_LAYERS_IMAGE_LAYER_H_

#include "cc/base/cc_export.h"
#include "cc/layers/content_layer.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace cc {

class ImageLayerUpdater;

// A Layer that contains only an Image element.
class CC_EXPORT ImageLayer : public TiledLayer {
 public:
  static scoped_refptr<ImageLayer> Create();

  // Layer implementation.
  virtual void SetTexturePriorities(const PriorityCalculator& priority_calc)
      OVERRIDE;
  virtual bool Update(ResourceUpdateQueue* queue,
                      const OcclusionTracker<Layer>* occlusion) OVERRIDE;
  virtual void CalculateContentsScale(float ideal_contents_scale,
                                      float* contents_scale_x,
                                      float* contents_scale_y,
                                      gfx::Size* content_bounds) OVERRIDE;
  virtual void OnOutputSurfaceCreated() OVERRIDE;

  void SetBitmap(const SkBitmap& image);

 protected:
  virtual bool HasDrawableContent() const OVERRIDE;

 private:
  ImageLayer();
  virtual ~ImageLayer();

  // TiledLayer Implementation.
  virtual LayerUpdater* Updater() const OVERRIDE;
  virtual void CreateUpdaterIfNeeded() OVERRIDE;

  float ImageContentsScaleX() const;
  float ImageContentsScaleY() const;

  SkBitmap bitmap_;

  scoped_refptr<ImageLayerUpdater> updater_;

  DISALLOW_COPY_AND_ASSIGN(ImageLayer);
};

}  // namespace cc

#endif  // CC_LAYERS_IMAGE_LAYER_H_
