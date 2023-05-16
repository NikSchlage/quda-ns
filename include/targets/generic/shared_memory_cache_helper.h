#pragma once

/**
   @file shared_memory_cache_helper.h
   @brief Convenience overloads to allow SharedMemoryCache objects to
   appear in simple expressions.  The actual implementation of
   SharedMemoryCache is target specific, and located in e.g.,
   include/targets/cuda/shared_memory_cache_helper.h, etc.
 */

namespace quda
{

  //template <typename T, int by, int bz, bool dynamic>
  //__device__ __host__ inline T operator+(const SharedMemoryCache<T, by, bz, dynamic> &a, const T &b)
  template <typename T, typename D>
  __device__ __host__ inline T operator+(const SharedMemoryCache<T, D> &a, const T &b)
  {
    return static_cast<const T &>(a) + b;
  }

  //template <typename T, int by, int bz, bool dynamic>
  //__device__ __host__ inline T operator+(const T &a, const SharedMemoryCache<T, by, bz, dynamic> &b)
  template <typename T, typename D>
  __device__ __host__ inline T operator+(const T &a, const SharedMemoryCache<T, D> &b)
  {
    return a + static_cast<const T &>(b);
  }

  //template <typename T, int by, int bz, bool dynamic>
  //__device__ __host__ inline T operator-(const SharedMemoryCache<T, by, bz, dynamic> &a, const T &b)
  template <typename T, typename D>
  __device__ __host__ inline T operator-(const SharedMemoryCache<T, D> &a, const T &b)
  {
    return static_cast<const T &>(a) - b;
  }

  //template <typename T, int by, int bz, bool dynamic>
  //__device__ __host__ inline T operator-(const T &a, const SharedMemoryCache<T, by, bz, dynamic> &b)
  template <typename T, typename D>
  __device__ __host__ inline T operator-(const T &a, const SharedMemoryCache<T, D> &b)
  {
    return a - static_cast<const T &>(b);
  }

  //template <typename T, int by, int bz, bool dynamic>
  //__device__ __host__ inline auto operator+=(SharedMemoryCache<T, by, bz, dynamic> &a, const T &b)
  template <typename T, typename D>
  __device__ __host__ inline auto operator+=(SharedMemoryCache<T, D> &a, const T &b)
  {
    a.save(static_cast<const T &>(a) + b);
    return a;
  }

  //template <typename T, int by, int bz, bool dynamic>
  //__device__ __host__ inline auto operator-=(SharedMemoryCache<T, by, bz, dynamic> &a, const T &b)
  template <typename T, typename D>
  __device__ __host__ inline auto operator-=(SharedMemoryCache<T, D> &a, const T &b)
  {
    a.save(static_cast<const T &>(a) - b);
    return a;
  }

  //template <typename T, int by, int bz, bool dynamic>
  //__device__ __host__ inline auto conj(const SharedMemoryCache<T, by, bz, dynamic> &a)
  template <typename T, typename D>
  __device__ __host__ inline auto conj(const SharedMemoryCache<T, D> &a)
  {
    return conj(static_cast<const T &>(a));
  }

  //template <typename T> struct get_type<
  //T, std::enable_if_t<std::is_same_v<T, SharedMemoryCache<typename T::value_type, T::block_size_y, T::block_size_z, T::dynamic>>>> {
  //using type = typename T::value_type;
  //};
  template <typename T> struct get_type<
    T, std::enable_if_t<std::is_same_v<T, SharedMemoryCache<typename T::value_type, typename T::D>>>> {
    using type = typename T::value_type;
  };

} // namespace quda
