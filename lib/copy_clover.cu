#include <tunable_nd.h>
#include <instantiate.h>
#include <kernels/copy_clover.cuh>

namespace quda {

  using namespace clover;

  template <typename OutOrder, typename InOrder, typename FloatOut, typename FloatIn>
  class CopyClover : TunableKernel2D {
    CopyCloverArg<FloatOut, FloatIn, OutOrder, InOrder> arg;
    CloverField &out;
    const CloverField &in;

    unsigned int sharedBytesPerThread() const { return 0; }
    unsigned int sharedBytesPerBlock(const TuneParam &) const { return 0 ;}

    unsigned int minThreads() const { return arg.threads.x; }

  public:
    CopyClover(CloverField &out, const CloverField &in, bool inverse, QudaFieldLocation location,
               void *Out, const void *In, float *outNorm, const float *inNorm) :
      TunableKernel2D(in, 2, location),
      arg(out, in, inverse, static_cast<FloatOut*>(Out), outNorm, static_cast<const FloatIn*>(In), inNorm),
      out(out),
      in(in)
    {
      apply(device::get_default_stream());
    }

    void apply(const qudaStream_t &stream) {
      TuneParam tp = tuneLaunch(*this, getTuning(), getVerbosity());
      launch<CloverCopy, true>(tp, stream, arg);
    }

    long long flops() const { return 0; } 
    long long bytes() const { return out.Bytes() + in.Bytes(); }
  };

  template <typename InOrder, typename FloatOut, typename FloatIn>
  void copyClover(CloverField &out, const CloverField &in, bool inverse, QudaFieldLocation location,
                  void *Out, const void *In, float *outNorm, const float *inNorm)
  {
    if (out.isNative()) {
      if (out.Reconstruct()) {
        using C = typename clover_mapper<FloatOut, 72, false, true>::type;
        CopyClover<C, InOrder, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
      } else {
        using C = typename clover_mapper<FloatOut, 72, false, false>::type;
        CopyClover<C, InOrder, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
      }
    } else if (out.Order() == QUDA_PACKED_CLOVER_ORDER) {
      CopyClover<QDPOrder<FloatOut>, InOrder, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
    } else if (out.Order() == QUDA_QDPJIT_CLOVER_ORDER) {
#ifdef BUILD_QDPJIT_INTERFACE
      CopyClover<QDPJITOrder<FloatOut>, InOrder, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
#else
      errorQuda("QDPJIT interface has not been built\n");
#endif
    } else if (out.Order() == QUDA_BQCD_CLOVER_ORDER) {
      errorQuda("BQCD output not supported");
    } else {
      errorQuda("Clover field %d order not supported", out.Order());
    }
  }

  template <typename FloatOut, typename FloatIn> struct CloverCopyOut {
    CloverCopyOut(CloverField &out, const CloverField &in, bool inverse, QudaFieldLocation location, 
                  void *Out, const void *In, float *outNorm, const float *inNorm)
    {
      if (in.isNative()) {
        if (in.Reconstruct()) {
          using C = typename clover_mapper<FloatIn, 72, false, true>::type;
          copyClover<C, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
        } else {
          using C = typename clover_mapper<FloatIn, 72, false, false>::type;
          copyClover<C, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
        }
      } else if (in.Order() == QUDA_PACKED_CLOVER_ORDER) {
        copyClover<QDPOrder<FloatIn>, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
      } else if (in.Order() == QUDA_QDPJIT_CLOVER_ORDER) {
#ifdef BUILD_QDPJIT_INTERFACE
        copyClover<QDPJITOrder<FloatIn>, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
#else
        errorQuda("QDPJIT interface has not been built\n");
#endif
      } else if (in.Order() == QUDA_BQCD_CLOVER_ORDER) {
#ifdef BUILD_BQCD_INTERFACE
        copyClover<BQCDOrder<FloatIn>, FloatOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
#else
        errorQuda("BQCD interface has not been built\n");
#endif
      } else {
        errorQuda("Clover field %d order not supported", in.Order());
      }
    }
  };

  template <typename FloatIn> struct CloverCopyIn {
    CloverCopyIn(const CloverField &in, CloverField &out, bool inverse, QudaFieldLocation location, 
                 void *Out, const void *In, float *outNorm, const float *inNorm)
    {
      // swizzle in/out back to instantiate out precision
      instantiatePrecision2<CloverCopyOut, FloatIn>(out, in, inverse, location, Out, In, outNorm, inNorm);
    }
  };

#ifdef GPU_CLOVER_DIRAC
  void copyGenericClover(CloverField &out, const CloverField &in, bool inverse, QudaFieldLocation location,
                         void *Out, const void *In, void *outNorm, const void *inNorm)
  {
    if (out.Precision() < QUDA_SINGLE_PRECISION && out.Order() > 4) 
      errorQuda("Fixed-point precision not supported for order %d", out.Order());
    if (in.Precision() < QUDA_SINGLE_PRECISION && in.Order() > 4) 
      errorQuda("Fixed-point precision not supported for order %d", in.Order());

    // swizzle in/out since we first want to instantiate precision
    instantiatePrecision<CloverCopyIn>(in, out, inverse, location, Out, In,
                                       reinterpret_cast<float*>(outNorm), reinterpret_cast<const float*>(inNorm));
  }
#else
  void copyGenericClover(CloverField &, const CloverField &, bool, QudaFieldLocation, void *, const void *, void *, const void *)
  {
    errorQuda("Clover has not been built");
  }
#endif

} // namespace quda
