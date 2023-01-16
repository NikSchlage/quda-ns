#pragma once

#include <device.h>
#include <tune_quda.h>
#include <target_device.h>
#include <kernel_helper.h>
#include <quda_sycl.h>
#include <quda_sycl_api.h>
#include <reduce_helper.h>

namespace quda {

  class TunableKernel : public Tunable
  {

  protected:
    QudaFieldLocation location;

    virtual unsigned int sharedBytesPerThread() const { return 0; }
    virtual unsigned int sharedBytesPerBlock(const TuneParam &) const { return 0; }

    template <template <typename> class Functor, bool grid_stride, typename Arg>
    qudaError_t
    launch_device(const kernel_t &kernel, const TuneParam &tp,
		  const qudaStream_t &stream, const Arg &arg)
    {
      using launcher_t = qudaError_t(*)(const TuneParam &, const qudaStream_t &,
					const Arg &);
      auto f = reinterpret_cast<launcher_t>(const_cast<void *>(kernel.func));
      launch_error = f(tp, stream, arg);
      return launch_error;
    }

  public:
    TunableKernel(QudaFieldLocation location = QUDA_INVALID_FIELD_LOCATION) : location(location) { }

    virtual bool advanceTuneParam(TuneParam &param) const
    {
      return location == QUDA_CPU_FIELD_LOCATION ? false : Tunable::advanceTuneParam(param);
    }

    TuneKey tuneKey() const { return TuneKey(vol, typeid(*this).name(), aux); }
  };

  // BlockOps<WarpCombine,BlockSync,SharedMem<A>,SharedMem<B>,BlockReduction<C>,
  //          Concurrent<SharedMem<D,36>,BlockReduction<E>>
  // hasSharedMem<BlockOps<T,U...>> = hasSharedMem<T> || hasSharedMem<U...>
  // hasSharedMem<SharedMem<T...>> = true;
  // sharedMemSizeFixed<BlockOps<T,U...>> = max(SharedMemSizeFixed<T>, sharedMemSizeFixed<U...>)
  // sharedMemSizeFixed<Concurrent<T,U...>> = SharedMemSizeFixed<T> + sharedMemSizeFixed<Concurrent<U...>>)
  // usesWarpCombine
  // usesBlockSync
  // usesThreadArray
  // usesSharedMem
  // usesBlockReducition

  struct NoBlockOps {};

  struct BlockSync {
    // target specific
    const sycl::nd_item<3> *ndi;
    void setBlockSync(const sycl::nd_item<3> &i) { ndi = &i; }
    // required for all targets
    BlockSync *getBlockSync() { return this; }
    void blockSync() {
#ifdef SYCL_DEVICE_ONLY
      sycl::group_barrier(ndi->get_group());
#endif
    }
  };

  template <typename T>
  struct SharedMem : BlockSync
  {
    // target specific
    using SharedMemT = T;
    T *smem;
    void setMem(T *mem) { smem = mem; }
    T *getMem() { return smem; }
    //T *sharedMem() { return smem; }
    // required for all targets
    SharedMem<T> *getSharedMem() { return this; }
  };

  template <typename T>
  struct BlockReduction : SharedMem<T>
  {
    // target specific
    using BlockReductionT = T;
    // required for all targets
    BlockReduction<T> *getBlockReduction() { return this; }
  };

  template <typename T, typename = void>
  struct hasBlockSyncS : std::false_type { };
  template <typename T>
  struct hasBlockSyncS<T,decltype(std::declval<T>().getBlockSync(),(void)0)> : std::true_type { };
  template <typename T>
  static constexpr bool hasBlockSync = hasBlockSyncS<T>::value;

  template <typename T, typename = void>
  struct hasSharedMemS : std::false_type { };
  template <typename T>
  struct hasSharedMemS<T,decltype(std::declval<T>().getSharedMem(),(void)0)> : std::true_type { };
  template <typename T>
  static constexpr bool hasSharedMem = hasSharedMemS<T>::value;

  template <typename T, typename = void>
  struct sharedMemTypeS { using type = void; };
  template <typename T>
  struct sharedMemTypeS<T,decltype((typename T::SharedMemT){},(void)0)> {
    using type = typename T::SharedMemT;
  };
  template <typename T>
  using sharedMemType = typename sharedMemTypeS<T>::type;

  template <typename T>
  static constexpr bool usesSharedMem = !std::is_same_v<sharedMemType<T>,void>;

  template <typename T>
  static constexpr bool hasBlockOps = hasBlockSync<T>; // SharedMem also has BlockSync

  // kernel helpers
  inline sycl::range<3> globalRange(const TuneParam &tp) {
    sycl::range<3> r(1,1,1);
    r[RANGE_X] = tp.grid.x*tp.block.x;
    r[RANGE_Y] = tp.grid.y*tp.block.y;
    r[RANGE_Z] = tp.grid.z*tp.block.z;
    return r;
  }
  inline sycl::range<3> localRange(const TuneParam &tp) {
    sycl::range<3> r(1,1,1);
    r[RANGE_X] = tp.block.x;
    r[RANGE_Y] = tp.block.y;
    r[RANGE_Z] = tp.block.z;
    return r;
  }

  template <typename F, typename Arg>
  std::enable_if_t<device::use_kernel_arg<Arg>(), qudaError_t>
  launch(const qudaStream_t &stream, sycl::nd_range<3> &ndRange, const Arg &arg)
  {
    if ( sizeof(Arg) > device::max_parameter_size() ) {
      errorQuda("Kernel arg too large: %lu > %u\n", sizeof(Arg), device::max_parameter_size());
    }
    qudaError_t err = QUDA_SUCCESS;
    auto q = device::get_target_stream(stream);
    try {
      if constexpr (usesSharedMem<F>) { // needs shared mem
	auto size = ndRange.get_local_range().size();
	if (size > device::max_dynamic_shared_memory()) {
	  warningQuda("Local mem request too large %lu > %lu\n", size, device::max_dynamic_shared_memory());
	  return QUDA_ERROR;
	}
	q.submit([&](sycl::handler &h) {
	  sycl::range<1> smem_range(size);
	  auto la = sycl::local_accessor<typename F::SharedMemT>(smem_range, h);
	  h.parallel_for<>
	    (ndRange,
	     //[=](sycl::nd_item<3> ndi) {
	     [=](sycl::nd_item<3> ndi) [[intel::reqd_sub_group_size(QUDA_WARP_SIZE)]] {
	       typename F::SharedMemT *smem = la.get_pointer();
	       F f(arg, ndi, smem);
	     });
	});
      } else { // no shared mem
	q.submit([&](sycl::handler &h) {
	  h.parallel_for<>
	    (ndRange,
	     //[=](sycl::nd_item<3> ndi) {
	     [=](sycl::nd_item<3> ndi) [[intel::reqd_sub_group_size(QUDA_WARP_SIZE)]] {
	       F f(arg, ndi);
	     });
	});
      }
    } catch (sycl::exception const& e) {
      auto what = e.what();
      target::sycl::set_error(what, "submit", __func__, __FILE__, __STRINGIFY__(__LINE__), activeTuning());
      if (getVerbosity() >= QUDA_DEBUG_VERBOSE) {
	printfQuda("  Caught synchronous SYCL exception:\n  %s\n",e.what());
      }
      err = QUDA_ERROR;
    }
    return err;
  }

  template <typename F, typename Arg>
  std::enable_if_t<!device::use_kernel_arg<Arg>(), qudaError_t>
  launch(const qudaStream_t &stream, sycl::nd_range<3> &ndRange, const Arg &arg)
  {
    static_assert(!usesSharedMem<F>);
    qudaError_t err = QUDA_SUCCESS;
    auto q = device::get_target_stream(stream);
    auto size = sizeof(arg);
    auto ph = device::get_arg_buf(stream, size);
    memcpy(ph, &arg, size);
    auto p = ph;
    try {
      q.submit([&](sycl::handler &h) {
	h.parallel_for<>
	  (ndRange,
	   //[=](sycl::nd_item<3> ndi) {
	   [=](sycl::nd_item<3> ndi) [[intel::reqd_sub_group_size(QUDA_WARP_SIZE)]] {
	     const Arg *arg2 = reinterpret_cast<const Arg*>(p);
	     F f(*arg2, ndi);
	   });
      });
    } catch (sycl::exception const& e) {
      auto what = e.what();
      target::sycl::set_error(what, "submit", __func__, __FILE__, __STRINGIFY__(__LINE__), activeTuning());
      if (getVerbosity() >= QUDA_DEBUG_VERBOSE) {
	printfQuda("  Caught synchronous SYCL exception:\n  %s\n",e.what());
      }
      err = QUDA_ERROR;
    }
    return err;
  }

  template <typename F, typename Arg>
  qudaError_t
  launchX(const qudaStream_t &stream, sycl::nd_range<3> &ndRange, const Arg &arg)
  {
    static_assert(!usesSharedMem<F>);
    qudaError_t err = QUDA_SUCCESS;
    auto q = device::get_target_stream(stream);
    auto size = sizeof(arg);
    auto ph = device::get_arg_buf(stream, size);
    memcpy(ph, &arg, size);
    auto p = ph;
    try {
      q.submit([&](sycl::handler &h) {
	h.parallel_for<>
	  (ndRange,
	   //[=](sycl::nd_item<3> ndi) {
	   [=](sycl::nd_item<3> ndi) [[intel::reqd_sub_group_size(QUDA_WARP_SIZE)]] {
	     const Arg *arg2 = reinterpret_cast<const Arg*>(p);
	     F f(*arg2, ndi);
	   });
      });
    } catch (sycl::exception const& e) {
      auto what = e.what();
      target::sycl::set_error(what, "submit", __func__, __FILE__, __STRINGIFY__(__LINE__), activeTuning());
      if (getVerbosity() >= QUDA_DEBUG_VERBOSE) {
	printfQuda("  Caught synchronous SYCL exception:\n  %s\n",e.what());
      }
      err = QUDA_ERROR;
    }
    return err;
  }

  template <template <typename> class Transformer, typename F, typename Arg>
  std::enable_if_t<device::use_kernel_arg<Arg>(), qudaError_t>
  launchR(const qudaStream_t &stream, sycl::nd_range<3> &ndRange, const Arg &arg)
  {
    static_assert(!usesSharedMem<F>);
    if ( sizeof(Arg) > device::max_parameter_size() ) {
      errorQuda("Kernel arg too large: %lu > %u\n", sizeof(Arg), device::max_parameter_size());
    }
    qudaError_t err = QUDA_SUCCESS;
    auto q = device::get_target_stream(stream);
    using reduce_t = typename Transformer<Arg>::reduce_t;
    using reducer_t = typename Transformer<Arg>::reducer_t;
    auto result_h = reinterpret_cast<reduce_t *>(quda::reducer::get_host_buffer());
    *result_h = reducer_t::init();
    reduce_t *result_d = result_h;
    if (commAsyncReduction()) {
      result_d = reinterpret_cast<reduce_t *>(quda::reducer::get_device_buffer());
      q.memcpy(result_d, result_h, sizeof(reduce_t));
    }
    auto reducer_h = sycl::reduction(result_d, *result_h, reducer_t());
    try {
      q.submit([&](sycl::handler& h) {
	h.parallel_for<>
	  (ndRange, reducer_h,
	   //[=](sycl::nd_item<3> ndi, auto &reducer_d) {
	   [=](sycl::nd_item<3> ndi, auto &reducer_d) [[intel::reqd_sub_group_size(QUDA_WARP_SIZE)]] {
	     F::apply(arg, ndi, reducer_d);
	   });
      });
    } catch (sycl::exception const& e) {
      auto what = e.what();
      target::sycl::set_error(what, "submit", __func__, __FILE__, __STRINGIFY__(__LINE__), activeTuning());
      if (getVerbosity() >= QUDA_DEBUG_VERBOSE) {
	printfQuda("  Caught synchronous SYCL exception:\n  %s\n",e.what());
      }
      err = QUDA_ERROR;
    }
    return err;
  }

  template <template <typename> class Transformer, typename F, typename Arg>
  std::enable_if_t<!device::use_kernel_arg<Arg>(), qudaError_t>
  launchR(const qudaStream_t &stream, sycl::nd_range<3> &ndRange, const Arg &arg)
  {
    static_assert(!usesSharedMem<F>);
    qudaError_t err = QUDA_SUCCESS;
    auto q = device::get_target_stream(stream);
    auto size = sizeof(arg);
    auto ph = device::get_arg_buf(stream, size);
    memcpy(ph, &arg, size);
    auto p = ph;
    using reduce_t = typename Transformer<Arg>::reduce_t;
    using reducer_t = typename Transformer<Arg>::reducer_t;
    auto result_h = reinterpret_cast<reduce_t *>(quda::reducer::get_host_buffer());
    *result_h = reducer_t::init();
    reduce_t *result_d = result_h;
    if (commAsyncReduction()) {
      result_d = reinterpret_cast<reduce_t *>(quda::reducer::get_device_buffer());
      q.memcpy(result_d, result_h, sizeof(reduce_t));
    }
    auto reducer_h = sycl::reduction(result_d, *result_h, reducer_t());
    try {
      q.submit([&](sycl::handler& h) {
	h.parallel_for<>
	  (ndRange, reducer_h,
	   //[=](sycl::nd_item<3> ndi, auto &reducer_d) {
	   [=](sycl::nd_item<3> ndi, auto &reducer_d) [[intel::reqd_sub_group_size(QUDA_WARP_SIZE)]] {
	     const Arg *arg2 = reinterpret_cast<const Arg*>(p);
	     F::apply(*arg2, ndi, reducer_d);
	   });
      });
    } catch (sycl::exception const& e) {
      auto what = e.what();
      target::sycl::set_error(what, "submit", __func__, __FILE__, __STRINGIFY__(__LINE__), activeTuning());
      if (getVerbosity() >= QUDA_DEBUG_VERBOSE) {
	printfQuda("  Caught synchronous SYCL exception:\n  %s\n",e.what());
      }
      err = QUDA_ERROR;
    }
    return err;
  }

}
