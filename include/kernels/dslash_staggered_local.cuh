#pragma once

#include <dslash_helper.cuh>
#include <color_spinor_field_order.h>
#include <gauge_field_order.h>
#include <color_spinor.h>
#include <dslash_helper.cuh>
#include <index_helper.cuh>

namespace quda
{
  /**
     @brief Parameter structure for driving the local Staggered Dslash operator
  */
  template <typename Float, int nColor_, QudaReconstructType reconstruct_u_,
            QudaReconstructType reconstruct_l_, bool improved_, QudaStaggeredLocalType step_, QudaStaggeredPhase phase_ = QUDA_STAGGERED_PHASE_MILC>
  struct LocalStaggeredArg : kernel_param<> {
    typedef typename mapper<Float>::type real;

    static constexpr int nDim = 4;

    static constexpr int nColor = nColor_;
    static constexpr int nSpin = 1;
    static constexpr bool spin_project = false;
    static constexpr bool spinor_direct_load = false; // false means texture load
    using F = typename colorspinor_mapper<Float, nSpin, nColor, spin_project, spinor_direct_load>::type;

    static constexpr QudaReconstructType reconstruct_u = reconstruct_u_;
    static constexpr QudaReconstructType reconstruct_l = reconstruct_l_;
    static constexpr bool gauge_direct_load = false; // false means texture load
    static constexpr QudaGhostExchange ghost = QUDA_GHOST_EXCHANGE_PAD;
    static constexpr bool use_inphase = improved_ ? false : true;
    static constexpr QudaStaggeredPhase phase = phase_;
    using GU = typename gauge_mapper<Float, reconstruct_u, 18, phase, gauge_direct_load, ghost, use_inphase>::type;
    using GL =
        typename gauge_mapper<Float, reconstruct_l, 18, QUDA_STAGGERED_PHASE_NO, gauge_direct_load, ghost, use_inphase>::type;

    F out;      /** output vector field */
    const F in; /** input vector field */
    const F x;  /** input vector when doing xpay */
    const GU U; /** the gauge field */
    const GL L; /** the long gauge field */

    int_fastdiv dim[nDim];    /** Dimensions of fine grid */
    bool is_partitioned[nDim]; /** Whether or not a dimension is partitioned */

    const real a; /** xpay scale factor */
    const int parity; /** parity we're gathering from */

    const real tboundary; /** temporal boundary condition */
    const bool is_first_time_slice; /** are we on the first (global) time slice */
    const bool is_last_time_slice; /** are we on the last (global) time slice */
    static constexpr bool improved = improved_; /** whether or not we're applying the improved operator */
    static constexpr QudaStaggeredLocalType step = step_; /** which step of the local staggered dslash we're applying */

    LocalStaggeredArg(ColorSpinorField &out, const ColorSpinorField &in, const GaugeField &U, const GaugeField &L, double a,
                 const ColorSpinorField &x, int parity) :
      kernel_param(dim3(out.VolumeCB(), 1, 1)),
      out(out),
      in(in),
      x(x),
      U(U),
      L(L),
      a(a),
      parity(parity),
      tboundary(U.TBoundary()),
      is_first_time_slice(comm_coord(3) == 0 ? true : false),
      is_last_time_slice(comm_coord(3) == comm_dim(3) - 1 ? true : false)
    {
      if (in.V() == out.V()) errorQuda("Aliasing pointers");
      checkOrder(out, in, x);        // check all orders match
      checkPrecision(out, in, x, U); // check all precisions match
      checkLocation(out, in, x, U);  // check all locations match
      if (!in.isNative() || !U.isNative())
        errorQuda("Unsupported field order colorspinor=%d gauge=%d combination\n", in.FieldOrder(), U.FieldOrder());

      dim[0] = ((x.SiteSubset() == QUDA_PARITY_SITE_SUBSET) ? 2 : 1) * x.X()[0];
      for (int i = 0; i < nDim; i++) {
        if (i != 0) dim[i] = x.X()[i];
        is_partitioned[i] = comm_dim_partitioned(i) ? true : false;
      }
    }
  };

  template <typename Arg>
  struct LocalStaggeredApply {

    static constexpr int nDim = 4;
    using real = typename Arg::real;
    using Vector = ColorSpinor<real, Arg::nColor, 1>;
    using Link = Matrix<complex<real>, Arg::nColor>;

    const Arg &arg;
    constexpr LocalStaggeredApply(const Arg &arg) : arg(arg) {}
    static constexpr const char *filename() { return KERNEL_FILE; }

    /**
       @brief Applies step 1 and 2 of the local dslash, which is D_{oe} for parity even and D_{eo} for parity odd
       @param[in] coord Precomputed Coord structure
       @return Accumulated output ColorVector
    */
    __device__ __host__ Vector localDslash(const Coord<nDim> &coord) const {
      static_assert(Arg::step != QUDA_STAGGERED_LOCAL_CLOVER, "localDslash called for a clover argument struct");
      // to be updated if we implement a full-parity version
      constexpr int their_spinor_parity = 0;

      Vector out;

      // helper lambda for getting U
      auto getU = [&] (int d_, int x_cb_, int parity_, int sign_) -> Link {
        return arg.improved ? arg.U(d_, x_cb_, parity_) : arg.U(d_, x_cb_, parity_, StaggeredPhase(coord, d_, sign_, arg));
      };

#pragma unroll
      for (int d = 0; d < nDim; d++) {

        // standard - forward direction
        if (!arg.is_partitioned[d] || (coord[d] + 1) < arg.dim[d])
        {
          const int fwd_idx = linkIndexP1(coord, arg.dim, d);
          const Link U = getU(d, coord.x_cb, arg.parity, +1);
          Vector in = arg.in(fwd_idx, their_spinor_parity);
          out = mv_add(U, in, out);
        }

        // improved - forward direction
        if (arg.improved && (!arg.is_partitioned[d] || (coord[d] + 3) < arg.dim[d]))
        {
          const int fwd3_idx = linkIndexP3(coord, arg.dim, d);
          const Link L = arg.L(d, coord.x_cb, arg.parity);
          const Vector in = arg.in(fwd3_idx, their_spinor_parity);
          out = mv_add(L, in, out);
        }

        // standard - backward direction
        if (!arg.is_partitioned[d] || (coord[d] - 1) >= 0)
        {
          const int back_idx = linkIndexM1(coord, arg.dim, d);
          const int gauge_idx = back_idx;
          const Link U = getU(d, gauge_idx, 1 - arg.parity, -1);
          Vector in = arg.in(back_idx, their_spinor_parity);
          out = mv_add(conj(U), -in, out);
        }

        // improved - backward direction
        if (arg.improved && (!arg.is_partitioned[d] || (coord[d] - 3) >= 0))
        {
          const int back3_idx = linkIndexM3(coord, arg.dim, d);
          const int gauge_idx = back3_idx;
          const Link L = arg.L(d, gauge_idx, 1 - arg.parity);
          const Vector in = arg.in(back3_idx, their_spinor_parity);
          out = mv_add(conj(L), -in, out);
        }

      } // dimension

      return out;
    }

    /**
       @brief Driver to apply the full local dslash
       @param[in] x_cb input coordinate
    */
    __device__ __host__ void operator()(int x_cb, int) {
      constexpr int nDim = 4;
      Coord<nDim> coord;

      // to be updated if we implement a full-parity version
      constexpr int my_spinor_parity = 0;

      Vector out;

      // Get coordinates
      coord.x_cb = x_cb;
      coord.X = getCoords(coord, x_cb, arg.dim, arg.parity);
      coord.s = 0;

      // helper lambda for getting U
      auto getU = [&] (int d_, int x_cb_, int parity_, int sign_) -> Link {
        return arg.improved ? arg.U(d_, x_cb_, parity_) : arg.U(d_, x_cb_, parity_, StaggeredPhase(coord, d_, sign_, arg));
      };

      if constexpr (Arg::step == QUDA_STAGGERED_LOCAL_CLOVER) {

        // helper lambda for getting U from ghost zone
        auto getUGhost = [&] (int d_, int ghost_idx_, int parity_, int sign_) -> Link {
          return arg.improved ? arg.U.Ghost(d_, ghost_idx_, parity_) : arg.U.Ghost(d_, ghost_idx_, parity_, StaggeredPhase(coord, d_, sign_, arg));
        };

        // the "in" vector at out's site --- this gets reused many times so (for now) we stick it in registers
        Vector in = arg.in(x_cb, my_spinor_parity);

#pragma unroll
        for (int d = 0; d < nDim; d++) {

          // standard - forward direction
          if (arg.is_partitioned[d] && (coord[d] + 1) >= arg.dim[d]) {
            // perform backwards (from previous pass) -- gathering to "X"
            Vector accum; 

            // backwards - standard (gather from X - 1, to X)
            {
              const Link U = getU(d, x_cb, arg.parity, +1);
              accum = mv_add(conj(U), -in, accum);
            }

            // backwards -- improved (gather from X - 3, to X)
            if (arg.improved) {
              const int back2_idx = linkIndexM2(coord, arg.dim, d);
              const int gauge_idx = back2_idx;
              const Link L = arg.L(d, gauge_idx, arg.parity);
              const Vector in_L = arg.in(back2_idx, my_spinor_parity);
              accum = mv_add(conj(L), -in_L, accum);
            }

            // forwards - standard (gather from X, to X - 1)
            {
              const Link U = getU(d, x_cb, arg.parity, +1);
              out = mv_add(U, accum, out);
            }
          }

          // improved - forward direction
          if (arg.improved && arg.is_partitioned[d] && (coord[d] + 3) >= arg.dim[d]) {
            // perform backwards (from previous pass) -- gathering to ["X", "X+1", "X+2"]
            Vector accum;

            // backwards - standard (gather from X - 1, to X)
            if ((coord[d] + 3) == arg.dim[d]) {
              const int fwd2_idx = linkIndexP2(coord, arg.dim, d);
              const int gauge_idx = fwd2_idx;
              const Link U = getU(d, gauge_idx, arg.parity, +1);
              const Vector in_U = arg.in(fwd2_idx, my_spinor_parity);
              accum = mv_add(conj(U), -in_U, accum);
            }

            // backwards - improved (gather from ["X-3","X-2","X-1"] to ["X","X+1","X+2"])
            {
              const Link L = arg.L(d, x_cb, arg.parity);
              accum = mv_add(conj(L), -in, accum);
            }

            // forwards - improved (gather from ["X", "X+1", "X+2"] to ["X-3","X-2","X-1"])
            {
              const Link L = arg.L(d, x_cb, arg.parity);
              out = mv_add(L, accum, out);
            }
          }

          // everything is awful, so let's really break this up
          if (arg.is_partitioned[d] && coord[d] == 0) {
            // first bit: gather from site [-1]

            // two contributions: one from self [0], one from forward [2]
            Vector accum;

            // contribution from self
            {
              const int ghost_idx2 = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link U = getUGhost(d, ghost_idx2, 1 - arg.parity, -1);
              accum = mv_add(U, in, accum);
            }

            // contribution from [2]
            if (arg.improved) {
              const int fwd2_idx = linkIndexP2(coord, arg.dim, d);
              const Vector in_L = arg.in(fwd2_idx, my_spinor_parity);

              // need some special sauce to get the index for the ghost L
              auto coord_copy = coord;
              coord_copy[d] = 2;
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord_copy, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);

              accum = mv_add(L, in_L, accum);
            }

            // and now, let's gather what we accumulated at [-1]
            {
              const int ghost_idx2 = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link U = getUGhost(d, ghost_idx2, 1 - arg.parity, -1);
              out = mv_add(conj(U), -accum, out);
            }

            // second bit: gather from site [-3]

            // one contribution: one from self [0]
            accum = Vector();

            // contribution from self
            if (arg.improved) {
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              accum = mv_add(L, in, accum);
            }

            // and now, let's gather what we accumulated at [-3]
            if (arg.improved) {
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              out = mv_add(conj(L), -accum, out);
            }
          }

          if (arg.improved && arg.is_partitioned[d] && coord[d] == 1) {
            // surprisingly easy: we only need to do the hop out/hop in by three for the "self"

            // first bit: gather from site [-2]

            // one contribution: one from self
            Vector accum;

            // contribution from self
            {
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              accum = mv_add(L, in, accum);
            }

            // second bit: gather what we accumulated at [-2] to [1]
            {
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              out = mv_add(conj(L), -accum, out);
            }
          }

          if (arg.improved && arg.is_partitioned[d] && coord[d] == 2) {
            // first bit: gather from site [-1]

            // two contributions: one from self, one from [0]
            Vector accum;

            // contribution from self:
            {
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              accum = mv_add(L, in, accum);
            }

            // contribution from [0]
            {
              const int bak2_idx = linkIndexM2(coord, arg.dim, d);
              Vector in = arg.in(bak2_idx, my_spinor_parity);
              auto coord_copy = coord;
              coord_copy[d] = 0;
              const int ghost_idx2 = ghostFaceIndexStaggered<0>(coord_copy, arg.dim, d, 1);
              const Link U = getUGhost(d, ghost_idx2, 1 - arg.parity, -1);

              accum = mv_add(U, in, accum);
            }

            // second bit: gather what we accumulated at [-1] to [2]
            {
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              out = mv_add(conj(L), -accum, out);
            }
          }

          /*
          // standard - backward direction
          if (arg.is_partitioned[d] && (coord[d] - 1) < 0) {
            // perform forwards (from previous pass) -- gathering to "-1"
            Vector accum;

            // forwards - standard (gather from 0, to -1)
            {
              const int ghost_idx2 = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              // last arg, direction, is always -1 --- it only effects getting the t boundary condition sign
              const Link U = getUGhost(d, ghost_idx2, 1 - arg.parity, -1); // link is in ghost zone
              accum = mv_add(U, in, accum);
            }

            // forwards -- improved (gather from 2, to -1)
            if (arg.improved) {
              // when updating replace arg.nFace with 1 here
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              const int fwd2_idx = linkIndexP2(coord, arg.dim, d);
              const Vector in_L = arg.in(fwd2_idx, my_spinor_parity);
              accum = mv_add(L, in_L, accum);
            }

            // backwards - standard (gather from -1, to 0)
            {
              const int ghost_idx2 = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link U = getUGhost(d, ghost_idx2, 1 - arg.parity, -1); // link is in ghost zone
              out = mv_add(conj(U), -accum, out);
            }
          }

          // improved - backward direction 
          if (arg.improved && arg.is_partitioned[d] && (coord[d] - 3) < arg.dim[d]) {
            // perform forwards (from previous pass) -- gathering to ["0", "1", "2"]
            Vector accum;

            // forwards - standard (gather from 0, to -1)
            if (coord[d] == 2) {
              const int back2_idx = linkIndexM2(coord, arg.dim, d);
              Coord<nDim> coord_copy = coord;
              coord_copy[d] -= 2; // need to properly update coord
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord_copy, arg.dim, d, 1);
              // last arg, direction, is always -1 --- it only effects getting the t boundary condition sign
              const Link U = getUGhost(d, ghost_idx, 1 - arg.parity, -1);
              const Vector in_U = arg.in(back2_idx, my_spinor_parity);
              accum = mv_add(U, in_U, accum);
            }

            // forwards - improved (gather from ["0","1","2"] to ["-3","-2","-1"])
            {
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              accum = mv_add(L, in, accum);
            }

            // backwards - improved (gather from ["-3", "-2", "-1"] to ["0","1","2"])
            {
              const int ghost_idx = ghostFaceIndexStaggered<0>(coord, arg.dim, d, 1);
              const Link L = arg.L.Ghost(d, ghost_idx, 1 - arg.parity);
              out = mv_add(conj(L), -accum, out);
            }
          }*/

        } // dimension

      } else {
        out = localDslash(coord);
      } // is clover

      if constexpr (Arg::step == QUDA_STAGGERED_LOCAL_STEP2) {
        Vector x = arg.x(coord.x_cb, my_spinor_parity);
        out = arg.a * x - out;
      }

      if constexpr (Arg::step == QUDA_STAGGERED_LOCAL_CLOVER) {
        Vector partial_out = arg.out(coord.x_cb, my_spinor_parity);
        out = partial_out - out;
      }

      arg.out(coord.x_cb, my_spinor_parity) = out;

    }
  };

} // namespace quda


