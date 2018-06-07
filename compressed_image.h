// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COMPRESSED_IMAGE_H_
#define COMPRESSED_IMAGE_H_

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "common.h"
#include "header.h"
#include "image.h"
#include "noise.h"
#include "pik_info.h"
#include "pik_params.h"
#include "quantizer.h"
#include "tile_flow.h"

namespace pik {

Image3F AlignImage(const Image3F& in, const size_t N);

void CenterOpsinValues(Image3F* img);

struct ColorTransform {
  ColorTransform(size_t xsize, size_t ysize)
      : ytob_dc(120), ytox_dc(128),
        ytob_map(DivCeil(xsize, kTileSize),
                 DivCeil(ysize, kTileSize), 120),
        ytox_map(DivCeil(xsize, kTileSize),
                 DivCeil(ysize, kTileSize), 128) {}
  int ytob_dc;
  int ytox_dc;
  Image<int> ytob_map;
  Image<int> ytox_map;
};

struct QuantizedCoeffs {
  Image3S dct;
};

void ComputePredictionResiduals(const Quantizer& quantizer, int flags,
                                Image3F* coeffs);

void ApplyColorTransform(const ColorTransform& ctan,
                         const float factor,
                         const ImageF& y_plane,
                         Image3F* coeffs);

QuantizedCoeffs ComputeCoefficients(const CompressParams& params,
                                    const Header& header,
                                    const Image3F& opsin,
                                    const Quantizer& quantizer,
                                    const ColorTransform& ctan,
                                    ThreadPool* pool,
                                    const PikInfo* aux_out = nullptr);

std::string EncodeToBitstream(const QuantizedCoeffs& qcoeffs,
                              const Quantizer& quantizer,
                              const NoiseParams& noise_params,
                              const ColorTransform& ctan, bool fast_mode,
                              PikInfo* info = nullptr);

bool DecodeFromBitstream(const uint8_t* data, const size_t data_size,
                         const size_t xsize, const size_t ysize,
                         ColorTransform* ctan,
                         NoiseParams* noise_params,
                         Quantizer* quantizer,
                         QuantizedCoeffs* qcoeffs,
                         size_t* compressed_size);

Image3F ReconOpsinImage(const Header& header,
                        const QuantizedCoeffs& qcoeffs,
                        const Quantizer& quantizer, const ColorTransform& ctan,
                        ThreadPool* pool,
                        PikInfo* pik_info = nullptr);

void GaborishInverse(Image3F &opsin);
Image3F ConvolveGaborish(const Image3F& in, ThreadPool* pool);
Image3F ConvolveGaborishTF(const Image3F& in, ThreadPool* pool);

}  // namespace pik

#endif  // COMPRESSED_IMAGE_H_
