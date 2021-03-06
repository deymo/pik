// Copyright 2017 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT.

#ifndef BUTTERAUGLI_DISTANCE_H_
#define BUTTERAUGLI_DISTANCE_H_

#include "codec.h"

namespace pik {

// Returns the butteraugli distance between rgb0 and rgb1.
// If distmap is not null, it must be the same size as rgb0 and rgb1.
float ButteraugliDistance(const CodecInOut* rgb0, const CodecInOut* rgb1,
                          float hf_asymmetry, ImageF* distmap = nullptr,
                          ThreadPool* pool = nullptr);

}  // namespace pik

#endif  // BUTTERAUGLI_DISTANCE_H_
