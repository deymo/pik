// Copyright 2017 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT.

// Common parameters that are needed for both the ANS entropy encoding and
// decoding methods.

#ifndef ANS_PARAMS_H_
#define ANS_PARAMS_H_

#include <stdint.h>
#include <cstdlib>

namespace pik {

static const int kANSBufferSize = 1 << 16;

#define ANS_LOG_TAB_SIZE 10
#define ANS_TAB_SIZE (1 << ANS_LOG_TAB_SIZE)
#define ANS_TAB_MASK (ANS_TAB_SIZE - 1)
#define ANS_SIGNATURE 0x13  // Initial state, used as CRC.

}  // namespace pik

#endif  // ANS_PARAMS_H_
