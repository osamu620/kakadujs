// Copyright (c) Chris Hafey.
// SPDX-License-Identifier: MIT

#pragma once

// Kakadu core includes
#include "kdu_elementary.h"
#include "kdu_messaging.h"
#include "kdu_params.h"
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "kdu_utils.h"
#include "jp2.h"
#include "Size.hpp"
#include <vector>

// Application level includes
#include "kdu_stripe_compressor.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/val.h>
#endif

#include "FrameInfo.hpp"

class kdu_buffer_target : public kdu_core::kdu_compressed_target
{
public: // Member functions
  kdu_buffer_target(std::vector<uint8_t> &encoded) : encoded_(encoded)
  {
    encoded_.resize(0);
  }
  ~kdu_buffer_target() { return; } // Destructor must be virtual
  int get_capabilities() { return KDU_TARGET_CAP_SEQUENTIAL /* KDU_TARGET_CAP_CACHED */; }
  bool write(const kdu_core::kdu_byte *buf, int num_bytes)
  {
    const size_t size = encoded_.size();
    encoded_.resize(size + num_bytes);
    memcpy(encoded_.data() + size, buf, num_bytes);
    return true;
  }

private: // Data
  std::vector<uint8_t> &encoded_;
};

/// <summary>
/// JavaScript API for encoding images to HTJ2K bitstreams with OpenJPH
/// </summary>
class HTJ2KEncoder
{
public:
  /// <summary>
  /// Constructor for encoding a HTJ2K image from JavaScript.
  /// </summary>
  HTJ2KEncoder() : decompositions_(5),
                   lossless_(true),
                   quantizationStep_(-1.0),
                   progressionOrder_(2), // RPCL
                   blockDimensions_(64, 64),
                   htEnabled_(true),
                   qfactor(85),
                   buf_(nullptr),
                   size_(0)
  {
  }

#ifdef __EMSCRIPTEN__
  /// <summary>
  /// Resizes the decoded buffer to accomodate the specified frameInfo.
  /// Returns a TypedArray of the buffer allocated in WASM memory space that
  /// will hold the pixel data to be encoded.  JavaScript code needs
  /// to copy the pixel data into the returned TypedArray.  This copy
  /// operation is needed because WASM runs in a sandbox and cannot access
  /// data managed by JavaScript
  /// </summary>
  /// <param name="frameInfo">FrameInfo that describes the pixel data to be encoded</param>
  /// <returns>
  /// TypedArray for the buffer allocated in WASM memory space for the
  /// source pixel data to be encoded.
  /// </returns>
  emscripten::val getDecodedBuffer(const FrameInfo &frameInfo)
  {
    frameInfo_ = frameInfo;
    const size_t bytesPerPixel = (frameInfo_.bitsPerSample + 8 - 1) / 8;
    const size_t decodedSize = frameInfo_.width * frameInfo_.height * frameInfo_.componentCount * bytesPerPixel;
    decoded_.resize(decodedSize);
    return emscripten::val(emscripten::typed_memory_view(decoded_.size(), decoded_.data()));
  }

  /// <summary>
  /// Returns a TypedArray of the buffer allocated in WASM memory space that
  /// holds the encoded pixel data.
  /// </summary>
  /// <returns>
  /// TypedArray for the buffer allocated in WASM memory space for the
  /// encoded pixel data.
  /// </returns>
  emscripten::val getEncodedBuffer()
  {
    return emscripten::val(emscripten::typed_memory_view(encoded_.size(), encoded_.data()));
  }
#else
  /// <summary>
  /// Returns the buffer to store the decoded bytes.  This method is not
  /// exported to JavaScript, it is intended to be called by C++ code
  /// </summary>
  std::vector<uint8_t> &getDecodedBytes(const FrameInfo &frameInfo)
  {
    frameInfo_ = frameInfo;
    return decoded_;
  }

  void setSourceImage(uint8_t *buf, size_t size) {
    buf_ = buf;
    size_ = size;
  }

  /// <summary>
  /// Returns the buffer to store the encoded bytes.  This method is not
  /// exported to JavaScript, it is intended to be called by C++ code
  /// </summary>
  const std::vector<uint8_t> &getEncodedBytes() const
  {
    return encoded_;
  }
#endif

  /// <summary>
  /// Sets the number of wavelet decompositions and clears any precincts
  /// </summary>
  void setDecompositions(size_t decompositions)
  {
    decompositions_ = decompositions;
  }

  /// <summary>
  /// Sets the quality level for the image.  If lossless is false then
  /// quantizationStep controls the lossy quantization applied.  quantizationStep
  /// is ignored if lossless is true
  /// </summary>
  void setQuality(bool lossless, float quantizationStep)
  {
    lossless_ = lossless;
    quantizationStep_ = quantizationStep;
  }

  /// <summary>
  /// Sets the Qfactor value (0 - 100)
  /// </summary>
  void setQfactor(int qf)
  {
    if (qf < 0) {
      qf = 0;
    }
    if (qf > 100) {
      qf = 100;
    }
    qfactor = qf;
  }

  /// <summary>
  /// Sets the progression order
  /// 0 = LRCP
  /// 1 = RLCP
  /// 2 = RPCL
  /// 3 = PCRL
  /// 4 = CPRL
  /// </summary>
  void setProgressionOrder(size_t progressionOrder)
  {
    progressionOrder_ = progressionOrder;
  }

  /// <summary>
  /// Sets the block dimensions
  /// </summary>
  void setBlockDimensions(Size blockDimensions)
  {
    blockDimensions_ = blockDimensions;
  }

  /// <summary>
  /// Sets HT encoding
  /// </summary>
  void setHTEnabled(bool htEnabled)
  {
    htEnabled_ = htEnabled;
  }

  /// <summary>
  /// Executes an HTJ2K encode using the data in the source buffer.  The
  /// JavaScript code must copy the source image frame into the source
  /// buffer before calling this method.  See documentation on getSourceBytes()
  /// above
  /// </summary>
  void encode()
  {
    // resize the encoded buffer so we don't have to keep resizing it
    const size_t bytesPerPixel = (frameInfo_.bitsPerSample + 8 - 1) / 8;
    encoded_.reserve(frameInfo_.width * frameInfo_.height * frameInfo_.componentCount * bytesPerPixel);

    //  Construct code-stream object
    kdu_core::siz_params siz;
    siz.set(Scomponents, 0, 0, frameInfo_.componentCount);
    siz.set(Sdims, 0, 0, frameInfo_.height);
    siz.set(Sdims, 0, 1, frameInfo_.width);
    siz.set(Sprecision, 0, 0, frameInfo_.bitsPerSample);
    siz.set(Ssigned, 0, 0, frameInfo_.isSigned);
    kdu_core::kdu_params *siz_ref = &siz;
    siz_ref->finalize();

    kdu_core::kdu_compressed_target *compressed_out = nullptr;
    kdu_buffer_target target(encoded_);
    compressed_out  = &target;
    // kdu_supp::jp2_family_tgt tgt;
    // tgt.open(&target);
    // kdu_supp::jp2_target output;
    // output.open(&tgt);
    // kdu_supp::jp2_dimensions dims = output.access_dimensions();
    // dims.init(&siz);
    // kdu_supp::jp2_colour colr = output.access_colour();
    // colr.init((frameInfo_.componentCount == 3) ? kdu_supp::JP2_sRGB_SPACE : kdu_supp::JP2_sLUM_SPACE);
    // output.write_header();
    // output.open_codestream(true);
    // compressed_out  = &output;

    kdu_core::kdu_codestream codestream;
    codestream.create(&siz, compressed_out);

    // Set up any specific coding parameters and finalize them.
    if (htEnabled_)
    {
      codestream.access_siz()->parse_string("Cmodes=HT");
    }
    char param[32];
    if (lossless_)
    {
      codestream.access_siz()->parse_string("Creversible=yes");
    }
    else
    {
      codestream.access_siz()->parse_string("Creversible=no");
      snprintf(param,32, "Qfactor=%d", qfactor);
      codestream.access_siz()->parse_string(param);
      // snprintf(param, 32, "Qstep=%f", quantizationStep_);
      // codestream.access_siz()->parse_string(param);
    }

    switch (progressionOrder_)
    {
    case 0:
      codestream.access_siz()->parse_string("Corder=LRCP");
      break;
    case 1:
      codestream.access_siz()->parse_string("Corder=RLCP");
      break;
    case 2:
      codestream.access_siz()->parse_string("Corder=RPCL");
      break;
    case 3:
      codestream.access_siz()->parse_string("Corder=PCRL");
      break;
    case 4:
      codestream.access_siz()->parse_string("Corder=CPRL");
      break;
    }

    snprintf(param,32, "Clevels=%zu", decompositions_);
    codestream.access_siz()->parse_string(param);

    snprintf(param, 32, "Cblk={%d,%d}", blockDimensions_.width, blockDimensions_.height);
    codestream.access_siz()->parse_string(param);

    codestream.access_siz()->finalize_all(); // Set up coding defaults

    // Now compress the image in one hit, using `kdu_stripe_compressor'
    kdu_supp::kdu_stripe_compressor compressor;
    kdu_supp::kdu_thread_env env;
    env.create();
    env.add_thread();
    env.add_thread();
    
    
    compressor.start(codestream, 0, nullptr, nullptr, 0U, false, false, true, 0.0, 0, true, &env);
    
    // compressor.start(codestream);
    int stripe_heights[3] = {frameInfo_.height, frameInfo_.height, frameInfo_.height};
    compressor.push_stripe(buf_, stripe_heights);
    // if (frameInfo_.bitsPerSample <= 8)
    // {
    //   compressor.push_stripe(
    //       decoded_.data(),
    //       stripe_heights);
    // }
    // else
    // {
    //   bool is_signed[3] = {frameInfo_.isSigned, frameInfo_.isSigned, frameInfo_.isSigned};
    //   int precisions[3] = {frameInfo_.bitsPerSample, frameInfo_.bitsPerSample, frameInfo_.bitsPerSample};
    //   compressor.push_stripe(
    //       (kdu_core::kdu_int16 *)decoded_.data(),
    //       stripe_heights,
    //       NULL,
    //       NULL,
    //       NULL,
    //       precisions,
    //       is_signed);
    // }
    compressor.finish();

    // Finally, cleanup
    codestream.destroy();

    // tgt.close();
    // output.close();
    target.close();
  }

private:
  std::vector<uint8_t> decoded_;
  std::vector<uint8_t> encoded_;
  FrameInfo frameInfo_;
  size_t decompositions_;
  bool lossless_;
  float quantizationStep_;
  size_t progressionOrder_;
  Size blockDimensions_;
  bool htEnabled_;
  int qfactor;
  uint8_t *buf_;
  size_t size_;
};
