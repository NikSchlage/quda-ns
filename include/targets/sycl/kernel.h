#pragma once
#include <device.h>
#include <utility>

namespace quda {

  template <template <typename> class Functor, typename Arg, bool grid_stride = false>
  void Kernel1D(Arg arg, sycl::nd_item<3> ndi)
  {
    Functor<Arg> f(arg);

    //auto i = threadIdx.x + blockIdx.x * blockDim.x;
    auto i = ndi.get_global_id(0);
    while (i < arg.threads.x) {
      f(i);
      //if (grid_stride) i += gridDim.x * blockDim.x; else break;
      if (grid_stride) i += ndi.get_global_range(0); else break;
    }
  }
  template <template <typename> class Functor, typename Arg, bool grid_stride = false>
  qudaError_t
  launchKernel1D(const TuneParam &tp, const qudaStream_t &stream, Arg arg)
  {
    //auto a = (Arg *)managed_malloc(sizeof(Arg));
    //memcpy(a, &arg, sizeof(Arg));
    sycl::range<3> globalSize{tp.grid.x*tp.block.x, tp.grid.y*tp.block.y, tp.grid.z*tp.block.z};
    sycl::range<3> localSize{tp.block.x, tp.block.y, tp.block.z};
    sycl::nd_range<3> ndRange{globalSize, localSize};
    auto q = device::get_target_stream(stream);
    warningQuda("launchKernel1D %s", grid_stride?"true":"false");
    warningQuda("%s  %s", str(globalSize).c_str(), str(localSize).c_str());
    warningQuda("%s", str(arg.threads).c_str());
    q.submit([&](sycl::handler& h) {
	       h.parallel_for<class Kernel1D>(ndRange,
				 [=](sycl::nd_item<3> ndi)
				 {
				    quda::Kernel1D<Functor, Arg, grid_stride>(arg, ndi);
				  });
	     });
    //managed_free(a);
    warningQuda("end launchKernel1D");
    return QUDA_SUCCESS;
  }

  template <template <typename> class Functor, typename Arg, bool grid_stride = false>
  void Kernel2D(Arg arg, sycl::nd_item<3> ndi)
  {
    Functor<Arg> f(arg);

    //auto i = threadIdx.x + blockIdx.x * blockDim.x;
    //auto j = threadIdx.y + blockIdx.y * blockDim.y;
    auto i = ndi.get_global_id(0);
    auto j = ndi.get_global_id(1);
    if (j >= arg.threads.y) return;

    while (i < arg.threads.x) {
      f(i, j);
      //if (grid_stride) i += gridDim.x * blockDim.x; else break;
      if (grid_stride) i += ndi.get_global_range(0); else break;
    }
  }
  template <template <typename> class Functor, typename Arg, bool grid_stride = false>
  qudaError_t
  launchKernel2D(const TuneParam &tp, const qudaStream_t &stream, Arg arg)
  {
    //auto a = (Arg *)managed_malloc(sizeof(Arg));
    //memcpy((void*)a, &arg, sizeof(Arg));
    sycl::range<3> globalSize{tp.grid.x*tp.block.x, tp.grid.y*tp.block.y, tp.grid.z*tp.block.z};
    sycl::range<3> localSize{tp.block.x, tp.block.y, tp.block.z};
    sycl::nd_range<3> ndRange{globalSize, localSize};
    auto q = device::get_target_stream(stream);
    warningQuda("launchKernel2D %s", grid_stride?"true":"false");
    warningQuda("%s  %s", str(globalSize).c_str(), str(localSize).c_str());
    warningQuda("%s", str(arg.threads).c_str());
    q.submit([&](sycl::handler& h) {
	       h.parallel_for<class Kernel2D>(ndRange,
					      [=](sycl::nd_item<3> ndi)
				  {
				    quda::Kernel2D<Functor, Arg, grid_stride>(arg, ndi);
				  });
	     });
    //managed_free(a);
    warningQuda("end launchKernel2D");
    return QUDA_SUCCESS;
  }

#if 0
  using Cacc = sycl::accessor<const char,1,sycl::access_mode::read,
			      sycl::target::constant_buffer>;

  template <typename T>
  inline void checkSetConstPtr(T &x, int y) {
    x.setConstPtr(0, (const char *)0);
  }
  template <typename T, typename U>
  inline void checkSetConstPtr(T &x, U y) {
    errorQuda("setConstPtr %s not available but const parameters were passed", typeid(x).name());
  }

  template <typename T>
  inline void setptrs(T &x) {}

  template <typename T>
  inline void setptrs(T &x, Cacc acc) {
    auto p = acc.get_pointer();
    x.setConstPtr(0, p);
  }
#endif

  template <template <typename> class Functor, typename Arg, bool grid_stride>
  void Kernel3D(Arg arg, sycl::nd_item<3> ndi)
  {
    //setptrs(arg, cnsts...);
    Functor<Arg> f(arg);

    //auto i = threadIdx.x + blockIdx.x * blockDim.x;
    //auto j = threadIdx.y + blockIdx.y * blockDim.y;
    //auto k = threadIdx.z + blockIdx.z * blockDim.z;
    auto j = ndi.get_global_id(1);
    if (j >= arg.threads.y) return;
    auto k = ndi.get_global_id(2);
    if (k >= arg.threads.z) return;

    auto i = ndi.get_global_id(0);
    while (i < arg.threads.x) {
      f(i, j, k);
      //if (grid_stride) i += gridDim.x * blockDim.x; else break;
      if (grid_stride) i += ndi.get_global_range(0); else break;
    }
  }

  template <template <typename> class Functor, typename Arg,
	    bool grid_stride = false>
  qudaError_t
  launchKernel3D(const TuneParam &tp, const qudaStream_t &stream, Arg arg)
  {
    sycl::range<3> globalSize{tp.grid.x*tp.block.x, tp.grid.y*tp.block.y, tp.grid.z*tp.block.z};
    sycl::range<3> localSize{tp.block.x, tp.block.y, tp.block.z};
    sycl::nd_range<3> ndRange{globalSize, localSize};
    auto q = device::get_target_stream(stream);
    warningQuda("launchKernel3D %s", grid_stride?"true":"false");
    warningQuda("%s  %s", str(globalSize).c_str(), str(localSize).c_str());
    warningQuda("%s", str(arg.threads).c_str());
    q.submit([&](sycl::handler& h) {
	       h.parallel_for<class Kernel3D>
		 (ndRange,
		  [=](sycl::nd_item<3> ndi) {
		    quda::Kernel3D<Functor, Arg, grid_stride>(arg, ndi);
		  });
	     });
    warningQuda("end launchKernel3D");
    return QUDA_SUCCESS;
  }

#if 0
  template <template <typename> class Functor, bool grid_stride, typename Arg>
  qudaError_t
  launchKernel3D1(const TuneParam &tp, const qudaStream_t &stream, Arg arg,...)
  {
    errorQuda("setConstPtr %s not available but const parameters were passed", typeid(arg).name());
    return QUDA_ERROR;
  }

  template <template <typename> class Functor, bool grid_stride, typename Arg,
	    typename=decltype(std::declval<Arg>().setConstPtr(0,nullptr))>
  qudaError_t
  launchKernel3D1(const TuneParam &tp, const qudaStream_t &stream, Arg arg,
		  constant_param_t param)
  {
    sycl::range<3> globalSize{tp.grid.x*tp.block.x, tp.grid.y*tp.block.y, tp.grid.z*tp.block.z};
    sycl::range<3> localSize{tp.block.x, tp.block.y, tp.block.z};
    sycl::nd_range<3> ndRange{globalSize, localSize};
    auto q = device::get_target_stream(stream);
    warningQuda("launchKernel3D %s", grid_stride?"true":"false");
    warningQuda("%s  %s", str(globalSize).c_str(), str(localSize).c_str());
    warningQuda("%s", str(arg.threads).c_str());
    sycl::buffer<const char,1> buf{param.host, sycl::range(param.bytes)};
    q.submit([&](sycl::handler& h) {
	       auto acc = buf.get_access<sycl::access_mode::read,sycl::target::constant_buffer>(h);
	       //auto p = acc0.get_pointer();
	       //auto p = (const char *)device_malloc(100);
	       //arg.setConstPtr(0, p);
	       h.parallel_for<class Kernel3D>
		 (ndRange,
		  [=](sycl::nd_item<3> ndi) {
		    quda::Kernel3D<Functor, Arg, grid_stride>(arg, ndi, acc);
		  });
	     });
    warningQuda("end launchKernel3D");
    return QUDA_SUCCESS;
  }
#endif

}
