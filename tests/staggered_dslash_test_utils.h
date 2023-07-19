#pragma once

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include <quda.h>
#include <gauge_field.h>
#include <dirac_quda.h>
#include <misc.h>
#include <host_utils.h>
#include <command_line_params.h>
#include <dslash_reference.h>
#include <staggered_dslash_reference.h>
#include <staggered_gauge_utils.h>

#include "dslash_test_helpers.h"
#include <assert.h>
#include <gtest/gtest.h>

using namespace quda;

dslash_test_type dtest_type = dslash_test_type::Dslash;
CLI::TransformPairs<dslash_test_type> dtest_type_map {
  {"Dslash", dslash_test_type::Dslash}, {"MatPC", dslash_test_type::MatPC}, {"Mat", dslash_test_type::Mat}
  // left here for completeness but not support in staggered dslash test
  // {"MatPCDagMatPC", dslash_test_type::MatPCDagMatPC},
  // {"MatDagMat", dslash_test_type::MatDagMat},
  // {"M5", dslash_test_type::M5},
  // {"M5inv", dslash_test_type::M5inv},
  // {"Dslash4pre", dslash_test_type::Dslash4pre}
};

struct DslashTime {
  double event_time;
  double cpu_time;
  double cpu_min;
  double cpu_max;

  DslashTime() : event_time(0.0), cpu_time(0.0), cpu_min(DBL_MAX), cpu_max(0.0) { }
};

struct StaggeredDslashTestWrapper {

  void *qdp_inlink[4] = {nullptr, nullptr, nullptr, nullptr};

  QudaGaugeParam gauge_param;
  QudaInvertParam inv_param;

  cpuGaugeField *cpuFat = nullptr;
  cpuGaugeField *cpuLong = nullptr;

  ColorSpinorField spinor;
  ColorSpinorField spinorOut;
  ColorSpinorField spinorRef;
  ColorSpinorField tmpCpu;
  ColorSpinorField cudaSpinor;
  ColorSpinorField cudaSpinorOut;

  std::vector<ColorSpinorField> vp_spinor;
  std::vector<ColorSpinorField> vp_spinor_out;

  // In the HISQ case, we include building fat/long links in this unit test
  void *qdp_fatlink[4] = {nullptr, nullptr, nullptr, nullptr};
  void *qdp_longlink[4] = {nullptr, nullptr, nullptr, nullptr};
  void **ghost_fatlink = nullptr, **ghost_longlink = nullptr;

  void *milc_fatlink = nullptr;
  void *milc_longlink = nullptr;

  QudaParity parity = QUDA_EVEN_PARITY;

  Dirac *dirac;

  // Split grid options
  bool test_split_grid = false;
  int num_src = 1;

  void staggeredDslashRef()
  {
    // compare to dslash reference implementation
    printfQuda("Calculating reference implementation...");
    switch (dtest_type) {
    case dslash_test_type::Dslash:
      staggeredDslash(spinorRef, qdp_fatlink, qdp_longlink, ghost_fatlink, ghost_longlink, spinor,
                      parity, dagger, inv_param.cpu_prec, gauge_param.cpu_prec, dslash_type);
      break;
    case dslash_test_type::MatPC:
      staggeredMatDagMat(spinorRef, qdp_fatlink, qdp_longlink, ghost_fatlink, ghost_longlink, spinor,
                         mass, 0, inv_param.cpu_prec, gauge_param.cpu_prec, tmpCpu, parity, dslash_type);
      break;
    case dslash_test_type::Mat:
      // the !dagger is to reconcile the QUDA convention of D_stag = {{ 2m, -D_{eo}}, -D_{oe}, 2m}} vs the host convention without the minus signs
      staggeredDslash(spinorRef.Even(), qdp_fatlink, qdp_longlink, ghost_fatlink, ghost_longlink,
                      spinor.Odd(), QUDA_EVEN_PARITY, !dagger, inv_param.cpu_prec, gauge_param.cpu_prec, dslash_type);
      staggeredDslash(spinorRef.Odd(), qdp_fatlink, qdp_longlink, ghost_fatlink, ghost_longlink,
                      spinor.Even(), QUDA_ODD_PARITY, !dagger, inv_param.cpu_prec, gauge_param.cpu_prec, dslash_type);
      if (dslash_type == QUDA_LAPLACE_DSLASH) {
        xpay(spinor.V(), kappa, spinorRef.V(), spinor.Length(), gauge_param.cpu_prec);
      } else {
        axpy(2 * mass, spinor.V(), spinorRef.V(), spinor.Length(), gauge_param.cpu_prec);
      }
      break;
    default: errorQuda("Test type %d not defined", static_cast<int>(dtest_type));
    }
  }

  void init_ctest(int precision, QudaReconstructType link_recon_)
  {
    gauge_param = newQudaGaugeParam();
    inv_param = newQudaInvertParam();

    setStaggeredGaugeParam(gauge_param);
    setStaggeredInvertParam(inv_param);

    prec = getPrecision(precision);

    gauge_param.cuda_prec = prec;
    gauge_param.cuda_prec_sloppy = prec;
    gauge_param.cuda_prec_precondition = prec;
    gauge_param.cuda_prec_refinement_sloppy = prec;

    inv_param.cuda_prec = prec;

    link_recon = link_recon_;

    init();
  }

  void init_test()
  {
    gauge_param = newQudaGaugeParam();
    inv_param = newQudaInvertParam();

    setStaggeredGaugeParam(gauge_param);
    setStaggeredInvertParam(inv_param);

    gauge_param.cuda_prec = prec;
    gauge_param.cuda_prec_sloppy = prec;
    gauge_param.cuda_prec_precondition = prec;
    gauge_param.cuda_prec_refinement_sloppy = prec;

    inv_param.cuda_prec = prec;

    init();
  }

  void init()
  {
    inv_param.split_grid[0] = grid_partition[0];
    inv_param.split_grid[1] = grid_partition[1];
    inv_param.split_grid[2] = grid_partition[2];
    inv_param.split_grid[3] = grid_partition[3];

    num_src = grid_partition[0] * grid_partition[1] * grid_partition[2] * grid_partition[3];
    test_split_grid = num_src > 1;
    if (test_split_grid) { dtest_type = dslash_test_type::Dslash; }

    inv_param.dagger = dagger ? QUDA_DAG_YES : QUDA_DAG_NO;

    setDims(gauge_param.X);
    dw_setDims(gauge_param.X, 1);
    if (Nsrc != 1) {
      warningQuda("Ignoring Nsrc = %d, setting to 1.", Nsrc);
      Nsrc = 1;
    }

    milc_fatlink = safe_malloc(4 * V * gauge_site_size * host_gauge_data_type_size);
    milc_longlink = safe_malloc(4 * V * gauge_site_size * host_gauge_data_type_size);

    for (int dir = 0; dir < 4; dir++) {
      qdp_inlink[dir] = safe_malloc(V * gauge_site_size * host_gauge_data_type_size);
      qdp_fatlink[dir] = safe_malloc(V * gauge_site_size * host_gauge_data_type_size);
      qdp_longlink[dir] = safe_malloc(V * gauge_site_size * host_gauge_data_type_size);
    }

    constructStaggeredHostGaugeField(qdp_inlink, qdp_longlink, qdp_fatlink, gauge_param, 0, nullptr);

    // Construct MILC order fields
    reorderQDPtoMILC(milc_fatlink, qdp_fatlink, V, gauge_site_size, gauge_param.cpu_prec, gauge_param.cpu_prec);
    reorderQDPtoMILC(milc_longlink, qdp_longlink, V, gauge_site_size, gauge_param.cpu_prec, gauge_param.cpu_prec);

    // Create ghost zones for CPU fields,
    // prepare and load the GPU fields

#ifdef MULTI_GPU
    gauge_param.type = (dslash_type == QUDA_ASQTAD_DSLASH) ? QUDA_ASQTAD_FAT_LINKS : QUDA_SU3_LINKS;
    gauge_param.reconstruct = QUDA_RECONSTRUCT_NO;
    GaugeFieldParam cpuFatParam(gauge_param, milc_fatlink);
    cpuFatParam.ghostExchange = QUDA_GHOST_EXCHANGE_PAD;
    cpuFat = new cpuGaugeField(cpuFatParam);
    ghost_fatlink = cpuFat->Ghost();

    if (dslash_type == QUDA_ASQTAD_DSLASH) {
      gauge_param.type = QUDA_ASQTAD_LONG_LINKS;
      GaugeFieldParam cpuLongParam(gauge_param, milc_longlink);
      cpuLongParam.ghostExchange = QUDA_GHOST_EXCHANGE_PAD;
      cpuLong = new cpuGaugeField(cpuLongParam);
      ghost_longlink = cpuLong ? cpuLong->Ghost() : nullptr;
    }
#endif

    gauge_param.type = (dslash_type == QUDA_ASQTAD_DSLASH) ? QUDA_ASQTAD_FAT_LINKS : QUDA_SU3_LINKS;
    if (dslash_type == QUDA_STAGGERED_DSLASH) {
      gauge_param.reconstruct = gauge_param.reconstruct_sloppy = link_recon;
    } else {
      gauge_param.reconstruct = gauge_param.reconstruct_sloppy = QUDA_RECONSTRUCT_NO;
    }

    // set verbosity prior to loadGaugeQuda
    setVerbosity(verbosity);

    printfQuda("Sending fat links to GPU\n");
    loadGaugeQuda(milc_fatlink, &gauge_param);

    gauge_param.type = QUDA_ASQTAD_LONG_LINKS;

#ifdef MULTI_GPU
    gauge_param.ga_pad *= 3;
#endif

    if (dslash_type == QUDA_ASQTAD_DSLASH) {
      gauge_param.staggered_phase_type = QUDA_STAGGERED_PHASE_NO;
      gauge_param.reconstruct = gauge_param.reconstruct_sloppy = link_recon;
      printfQuda("Sending long links to GPU\n");
      loadGaugeQuda(milc_longlink, &gauge_param);
    }

    ColorSpinorParam csParam;
    csParam.nColor = 3;
    csParam.nSpin = 1;
    csParam.nDim = 4;
    for (int d = 0; d < 4; d++) { csParam.x[d] = gauge_param.X[d]; }
    csParam.x[4] = 1;

    csParam.setPrecision(inv_param.cpu_prec);
    // inv_param.solution_type = QUDA_MAT_SOLUTION;
    csParam.pad = 0;
    if (dtest_type != dslash_test_type::Mat && dslash_type != QUDA_LAPLACE_DSLASH) {
      csParam.siteSubset = QUDA_PARITY_SITE_SUBSET;
      csParam.x[0] /= 2;
      inv_param.solution_type = QUDA_MATPC_SOLUTION;
    } else {
      csParam.siteSubset = QUDA_FULL_SITE_SUBSET;
      inv_param.solution_type = QUDA_MAT_SOLUTION;
    }

    csParam.siteOrder = QUDA_EVEN_ODD_SITE_ORDER;
    csParam.fieldOrder = QUDA_SPACE_SPIN_COLOR_FIELD_ORDER;
    csParam.gammaBasis = inv_param.gamma_basis; // this parameter is meaningless for staggered
    csParam.create = QUDA_ZERO_FIELD_CREATE;
    csParam.pc_type = QUDA_4D_PC;
    csParam.location = QUDA_CPU_FIELD_LOCATION;

    spinor = ColorSpinorField(csParam);
    spinorOut = ColorSpinorField(csParam);
    spinorRef = ColorSpinorField(csParam);
    tmpCpu = ColorSpinorField(csParam);

    spinor.Source(QUDA_RANDOM_SOURCE);

    if (test_split_grid) {
      inv_param.num_src = num_src;
      inv_param.num_src_per_sub_partition = 1;
      resize(vp_spinor, num_src, csParam);
      resize(vp_spinor_out, num_src, csParam);
      std::fill(vp_spinor.begin(), vp_spinor.end(), spinor);
    }

    csParam.fieldOrder = colorspinor::getNative(inv_param.cuda_prec, 1);
    csParam.pad = 0;
    csParam.setPrecision(inv_param.cuda_prec);
    csParam.location = QUDA_CUDA_FIELD_LOCATION;

    cudaSpinor = ColorSpinorField(csParam);
    cudaSpinorOut = ColorSpinorField(csParam);
    cudaSpinor = spinor;

    bool pc = (dtest_type == dslash_test_type::MatPC); // For test_type 0, can use either pc or not pc
    // because both call the same "Dslash" directly.
    DiracParam diracParam;
    setDiracParam(diracParam, &inv_param, pc);
    dirac = Dirac::create(diracParam);
  }

  void end()
  {
    for (int dir = 0; dir < 4; dir++) {
      if (qdp_inlink[dir] != nullptr) {
        host_free(qdp_inlink[dir]);
        qdp_inlink[dir] = nullptr;
      }
      if (qdp_fatlink[dir] != nullptr) {
        host_free(qdp_fatlink[dir]);
        qdp_fatlink[dir] = nullptr;
      }
      if (qdp_longlink[dir] != nullptr) {
        host_free(qdp_longlink[dir]);
        qdp_longlink[dir] = nullptr;
      }
    }

    if (dirac != nullptr) {
      delete dirac;
      dirac = nullptr;
    }

    host_free(milc_fatlink);
    milc_fatlink = nullptr;
    host_free(milc_longlink);
    milc_longlink = nullptr;

    freeGaugeQuda();

    if (cpuFat) {
      delete cpuFat;
      cpuFat = nullptr;
    }
    if (cpuLong) {
      delete cpuLong;
      cpuLong = nullptr;
    }
    commDimPartitionedReset();
  }

  DslashTime dslashCUDA(int niter)
  {
    DslashTime dslash_time;

    host_timer_t host_timer;
    device_timer_t device_timer;

    comm_barrier();
    device_timer.start();

    if (test_split_grid) {

      std::vector<void *> _hp_x(inv_param.num_src);
      std::vector<void *> _hp_b(inv_param.num_src);
      for (int i = 0; i < inv_param.num_src; i++) {
        _hp_x[i] = vp_spinor_out[i].V();
        _hp_b[i] = vp_spinor[i].V();
      }
      dslashMultiSrcStaggeredQuda(_hp_x.data(), _hp_b.data(), &inv_param, parity, milc_fatlink, milc_longlink,
                                  &gauge_param);

    } else {

      for (int i = 0; i < niter; i++) {

        host_timer.start();

        switch (dtest_type) {
        case dslash_test_type::Dslash: dirac->Dslash(cudaSpinorOut, cudaSpinor, parity); break;
        case dslash_test_type::MatPC: dirac->M(cudaSpinorOut, cudaSpinor); break;
        case dslash_test_type::Mat: dirac->M(cudaSpinorOut, cudaSpinor); break;
        default: errorQuda("Test type %d not defined on staggered dslash", static_cast<int>(dtest_type));
        }

        host_timer.stop();

        dslash_time.cpu_time += host_timer.last();
        // skip first and last iterations since they may skew these metrics if comms are not synchronous
        if (i > 0 && i < niter) {
          dslash_time.cpu_min = std::min(dslash_time.cpu_min, host_timer.last());
          dslash_time.cpu_max = std::max(dslash_time.cpu_max, host_timer.last());
        }
      }
    }

    device_timer.stop();
    dslash_time.event_time = device_timer.last();

    return dslash_time;
  }

  void run_test(int niter, bool print_metrics = false)
  {
    printfQuda("Tuning...\n");
    dslashCUDA(1);

    // reset flop counter
    dirac->Flops();

    DslashTime dslash_time = dslashCUDA(niter);
    spinorOut = cudaSpinorOut;

    if (print_metrics) {
      printfQuda("%fus per kernel call\n", 1e6 * dslash_time.event_time / niter);

      unsigned long long flops = dirac->Flops();
      double gflops = 1.0e-9 * flops / dslash_time.event_time;
      printfQuda("GFLOPS = %f\n", gflops);
      ::testing::Test::RecordProperty("Gflops", std::to_string(gflops));

      size_t ghost_bytes = cudaSpinor.GhostBytes();

      ::testing::Test::RecordProperty("Halo_bidirectitonal_BW_GPU",
                                      1.0e-9 * 2 * ghost_bytes * niter / dslash_time.event_time);
      ::testing::Test::RecordProperty("Halo_bidirectitonal_BW_CPU",
                                      1.0e-9 * 2 * ghost_bytes * niter / dslash_time.cpu_time);
      ::testing::Test::RecordProperty("Halo_bidirectitonal_BW_CPU_min", 1.0e-9 * 2 * ghost_bytes / dslash_time.cpu_max);
      ::testing::Test::RecordProperty("Halo_bidirectitonal_BW_CPU_max", 1.0e-9 * 2 * ghost_bytes / dslash_time.cpu_min);
      ::testing::Test::RecordProperty("Halo_message_size_bytes", 2 * ghost_bytes);

      printfQuda(
        "Effective halo bi-directional bandwidth (GB/s) GPU = %f ( CPU = %f, min = %f , max = %f ) for aggregate "
        "message size %lu bytes\n",
        1.0e-9 * 2 * ghost_bytes * niter / dslash_time.event_time,
        1.0e-9 * 2 * ghost_bytes * niter / dslash_time.cpu_time, 1.0e-9 * 2 * ghost_bytes / dslash_time.cpu_max,
        1.0e-9 * 2 * ghost_bytes / dslash_time.cpu_min, 2 * ghost_bytes);
    }
  }

  double verify()
  {
    double deviation = 0.0;

    if (test_split_grid) {
      for (int n = 0; n < num_src; n++) {
        auto spinor_ref_norm = blas::norm2(spinorRef);
        auto spinor_out_norm = blas::norm2(vp_spinor_out[n]);
        auto max_deviation = blas::max_deviation(spinorRef, vp_spinor_out[n]);

        bool failed = false;
        // Catching nans is weird.
        if (std::isnan(spinor_ref_norm)) { failed = true; }
        if (std::isnan(spinor_out_norm)) { failed = true; }

        printfQuda("Results: reference = %f, QUDA = %f, L2 relative deviation = %e, max deviation = %e\n",
                   spinor_ref_norm, spinor_out_norm, 1.0 - sqrt(spinor_out_norm / spinor_ref_norm), max_deviation[0]);
        deviation = std::max(deviation, pow(10.0, -(double)(ColorSpinorField::Compare(spinorRef, vp_spinor_out[n]))));
        if (failed) { deviation = 1.0; }
      }
    } else {
      auto spinor_ref_norm = blas::norm2(spinorRef);
      auto spinor_out_norm = blas::norm2(spinorOut);
      auto max_deviation = blas::max_deviation(spinorRef, spinorOut);

      bool failed = false;
      // Catching nans is weird.
      if (std::isnan(spinor_ref_norm)) { failed = true; }
      if (std::isnan(spinor_out_norm)) { failed = true; }

      printfQuda("Results: reference = %f, QUDA = %f, L2 relative deviation = %e, max deviation = %e\n",
                 spinor_ref_norm, spinor_out_norm, 1.0 - sqrt(spinor_out_norm / spinor_ref_norm), max_deviation[0]);
      deviation = pow(10, -(double)(ColorSpinorField::Compare(spinorRef, spinorOut)));
      if (failed) { deviation = 1.0; }
    }

    return deviation;
  }
};
