// QUDA headers
#include <quda.h>

// External headers
#include <host_utils.h>
#include <command_line_params.h>

//
// Created by luis on 03.08.20.
//
int main(int argc, char **argv)
{
  //This function sets default values to the MG parameter struct
  setQudaDefaultMgTestParams();
  // Parameter class that reads line arguments. It modifies global parameter variables
  auto app = make_app();
  add_multigrid_option_group(app);
  add_eigen_option_group(app);
  add_propagator_option_group(app);
  try {
    app->parse(argc, argv);
  } catch (const CLI::ParseError &e) {
    return app->exit(e);
  }
  setQudaPrecisions();

  // init QUDA
  initComms(argc, argv, gridsize_from_cmdline);

  //check dslash
  if (dslash_type != QUDA_WILSON_DSLASH && dslash_type != QUDA_CLOVER_WILSON_DSLASH
      && dslash_type != QUDA_DOMAIN_WALL_4D_DSLASH && dslash_type != QUDA_DOMAIN_WALL_DSLASH) {
    printfQuda("dslash_type %d not supported\n", dslash_type);
    exit(0);
  }

  if (inv_multigrid) {
    // Only these fermions are supported with MG
    if (dslash_type != QUDA_WILSON_DSLASH && dslash_type != QUDA_CLOVER_WILSON_DSLASH) {
      printfQuda("dslash_type %d not supported for MG\n", dslash_type);
      exit(0);
    }

    // Only these solve types are supported with MG
    if (solve_type != QUDA_DIRECT_SOLVE && solve_type != QUDA_DIRECT_PC_SOLVE) {
      printfQuda("Solve_type %d not supported with MG. Please use QUDA_DIRECT_SOLVE or QUDA_DIRECT_PC_SOLVE\n\n",
                 solve_type);
      exit(0);
    }
  }

  initQuda(device);

  // gauge params
  QudaGaugeParam gauge_param = newQudaGaugeParam(); // create an instance of a class that can hold the gauge parameters
  setWilsonGaugeParam(gauge_param); // set the content of this instance to the globally set values (default or from command line)
  QudaInvertParam inv_param = newQudaInvertParam();
  QudaMultigridParam mg_param = newQudaMultigridParam();
  QudaInvertParam mg_inv_param = newQudaInvertParam();
  QudaEigParam mg_eig_param[mg_levels];
  //QudaEigParam eig_param = newQudaEigParam();

  if (inv_multigrid) {
    setQudaMgSolveTypes();
    setMultigridInvertParam(inv_param);
    // Set sub structures
    mg_param.invert_param = &mg_inv_param;
    for (int i = 0; i < mg_levels; i++) {
      if (mg_eig[i]) {
        mg_eig_param[i] = newQudaEigParam();
        setMultigridEigParam(mg_eig_param[i], i);
        mg_param.eig_param[i] = &mg_eig_param[i];
      } else {
        mg_param.eig_param[i] = nullptr;
      }
    }
    // Set MG
    setMultigridParam(mg_param);
  } else {
    setInvertParam(inv_param);
  }
  inv_param.eig_param = nullptr;

  if (dslash_type == QUDA_DOMAIN_WALL_DSLASH || dslash_type == QUDA_DOMAIN_WALL_4D_DSLASH) {
    dw_setDims(gauge_param.X, inv_param.Ls);
  } else {
    setDims(gauge_param.X);
  }

  // allocate gaugefield on host
  void *gauge[4];
  for (auto &dir : gauge) dir = malloc(V * gauge_site_size * host_gauge_data_type_size);
  constructHostGaugeField(gauge, gauge_param, argc, argv);
  loadGaugeQuda((void *)gauge, &gauge_param); // copy gaugefield to GPU

  printfQuda("-----------------------------------------------------------------------------------\n");
  // compute plaquette
  double plaq[3];
  plaqQuda(plaq);
  printfQuda("Computed plaquette is %e (spatial = %e, temporal = %e)\n", plaq[0], plaq[1], plaq[2]);

  // Allocate host side memory for clover terms if needed.
  void *clover = nullptr;
  void *clover_inv = nullptr;
  // Allocate space on the host
  if (dslash_type == QUDA_CLOVER_WILSON_DSLASH || dslash_type == QUDA_TWISTED_CLOVER_DSLASH) {
    if (!compute_clover) { errorQuda("Specified clover dslash-type but did not specify compute-clover!"); }
    clover = malloc(V * clover_site_size * host_clover_data_type_size);
    clover_inv = malloc(V * clover_site_size * host_spinor_data_type_size);
    constructHostCloverField(clover, clover_inv, inv_param);
    if (inv_multigrid) {
      // This line ensures that if we need to construct the clover inverse (in either the smoother or the solver) we do so
      if (mg_param.smoother_solve_type[0] == QUDA_DIRECT_PC_SOLVE || solve_type == QUDA_DIRECT_PC_SOLVE) {
        inv_param.solve_type = QUDA_DIRECT_PC_SOLVE;
      }
    }
    loadCloverQuda(clover, clover_inv, &inv_param);
    if (inv_multigrid) {
      // Restore actual solve_type we want to do
      inv_param.solve_type = solve_type;
    }
  }

  // Now QUDA is initialised and the fields are loaded, we may setup the preconditioner
  void *mg_preconditioner = nullptr;
  if (inv_multigrid) {
    mg_preconditioner = newMultigridQuda(&mg_param);
    inv_param.preconditioner = mg_preconditioner;
  }

  // ColorSpinors (Wilson)
  // FIXME what about this parameter class? should it be part of quda.h like invertparam etc?
  quda::ColorSpinorParam cs_param;
  quda::ColorSpinorParam *cs_param_ptr = &cs_param;
  constructWilsonSpinorParam(&cs_param, &inv_param, &gauge_param);
  int spinor_dim = cs_param.nColor * cs_param.nSpin;
  setSpinorSiteSize(spinor_dim * 2); // this sets the global variable my_spinor_site_size

  size_t bytes_per_float = sizeof(double);
  // Allocate memory on host for one source (0,0,0,0) for each of the 12x12 color+spinor combinations
  auto *source_array = (double *)malloc(spinor_dim * spinor_dim * V * 2 * bytes_per_float);
  auto *prop_array = (double *)malloc(spinor_dim * spinor_dim * V * 2 * bytes_per_float);

  // This will be a C array of pointers to the memory (CSF->V()) of the
  // spinor_dim colorspinorfields. Functions declared in quda.h
  // can only accept C code for backwards compatibility reasons.
  void *source_array_ptr[spinor_dim];
  void *prop_array_ptr[spinor_dim];

  // Actually create ColorSpinorField objects and tell them to use the memory from above
  for (int i = 0; i < spinor_dim; i++) {
    int offset = i * V * spinor_dim * 2;
    source_array_ptr[i] = source_array + offset;
    prop_array_ptr[i] = prop_array + offset;
  }

  // This is where the result will be stored
  void *correlation_function_sum = nullptr;
  size_t corr_dim = 0, local_corr_length = 0;
  // We need this to calculate the finite momentum corrs. for temporal corrs we sum up x*px + y*pz + z*pz
  int Nmom=(momentum[0]+1)*(momentum[1]+1);
  int Mom[4]={momentum[0], momentum[1], momentum[2], momentum[3]};
  if (contract_type == QUDA_CONTRACT_TYPE_DR_FT_Z) {
    corr_dim = 2;
    Mom[2]=0;
    Nmom *= (momentum[3]+1);
  } else if (contract_type == QUDA_CONTRACT_TYPE_DR_FT_T) {
    corr_dim = 3;
    Mom[3]=0;
    Nmom *= (momentum[2]+1);
  } else {
    errorQuda("Unsupported contraction type %d given", contract_type);
  }
  local_corr_length = gauge_param.X[corr_dim];

  // calculate some parameters
  size_t global_corr_length = local_corr_length * comm_dim(corr_dim);
  size_t n_numbers_per_slice = 2 * cs_param.nSpin * cs_param.nSpin;
  size_t corr_size_in_bytes = n_numbers_per_slice * global_corr_length * sizeof(double);

  correlation_function_sum = malloc(Nmom*corr_size_in_bytes);

  // Loop over the number of sources to use. Default is prop_n_sources=1 and source position = 0 0 0 0
  for (int n = 0; n < prop_n_sources; n++) {
    printfQuda("Source position: %d %d %d %d\n", prop_source_position[n][0], prop_source_position[n][1],
               prop_source_position[n][2], prop_source_position[n][3]);
    const int source[4] = {prop_source_position[n][0], prop_source_position[n][1], prop_source_position[n][2],
                           prop_source_position[n][3]};

    // The overall shift of the position of the corr. need this when the source is not at origin.
    const int overall_shift_dim = source[corr_dim];

    // Loop over spin X color dilution positions, construct the sources
    // FIXME add the smearing too
    for (int i = 0; i < spinor_dim; i++) {

      constructPointSpinorSource(source_array_ptr[i], cs_param.nSpin, cs_param.nColor, inv_param.cpu_prec,
                                 gauge_param.X, i, source);
      inv_param.solver_normalization = QUDA_SOURCE_NORMALIZATION; // Make explicit for now.
      invertQuda(prop_array_ptr[i], source_array_ptr[i], &inv_param);
    }
    // Coming soon....
    //propagatorQuda(prop_array_ptr, source_array_ptr, &inv_param, &correlation_function_sum, contract_type, (void *)cs_param_ptr, gauge_param.X);

    // Zero out the result array
    memset(correlation_function_sum, 0, Nmom*corr_size_in_bytes);
    contractFTQuda(prop_array_ptr, prop_array_ptr, &correlation_function_sum, contract_type, &inv_param,
		   (void *)cs_param_ptr, gauge_param.X, source, Mom);
    printfQuda("    g= 0,  1,  2,  3,    4,    5,    6,    7, 8,  9,  10,  11,  12,  13,  14,  15 correspond to channel:\n"
               "Gamma=g1, g2, g3, g4, g5g1, g5g2, g5g3, g5g4, 1, g5, s12, s13, s14, s23, s24, s34\n");
    for (int px=0; px <= Mom[0]; px++) {
      for (int py=0; py <= Mom[1]; py++) {
        for (int pz=0; pz <= Mom[2]; pz++) {
          for (int pt=0; pt <= Mom[3]; pt++ ) {
            // Print correlators for this propagator source position
            for (int G_idx = 0; G_idx < 16; G_idx++) {
              for (size_t t = 0; t < global_corr_length; t++) {
                int index_real = (px+py*(Mom[0]+1)+pz*(Mom[0]+1)*(Mom[1]+1)+pt*(Mom[0]+1)*(Mom[1]+1)*(Mom[2]+1))
                    *n_numbers_per_slice * global_corr_length+n_numbers_per_slice * ((t + overall_shift_dim) % global_corr_length) + 2 * G_idx;
                int index_imag = index_real+1;
                double sign = G_idx < 8 ? -1. : 1.;//the minus sign from g5gm -> gmg5
                printfQuda("sum: prop_n=%d px=%d py=%d pz=%d pt=%d g=%d t=%lu %e %e\n", n, px, py, pz, pt, G_idx, t,
                           ((double *)correlation_function_sum)[index_real]*sign,
                           ((double *)correlation_function_sum)[index_imag]*sign);
              }
            }
          }
        }
      }
    }
  }

  if(open_flavor){
    //first we calculate heavy-light correlators
    kappa=kappa_light;
    setInvertParam(inv_param);
    if (dslash_type == QUDA_CLOVER_WILSON_DSLASH || dslash_type == QUDA_TWISTED_CLOVER_DSLASH) {
      constructHostCloverField(clover, clover_inv, inv_param);
      loadCloverQuda(clover, clover_inv, &inv_param);
    }
    constructWilsonSpinorParam(&cs_param, &inv_param, &gauge_param);
    auto *prop_array_light = (double *)malloc(spinor_dim * spinor_dim * V * 2 * bytes_per_float);
    void *prop_array_ptr_light[spinor_dim];
    for (int i = 0; i < spinor_dim; i++) {
      int offset = i * V * spinor_dim * 2;
      prop_array_ptr_light[i] = prop_array_light + offset;
    }

    for (int n = 0; n < prop_n_sources; n++) {
      printfQuda("Source position: %d %d %d %d\n", prop_source_position[n][0], prop_source_position[n][1],
                 prop_source_position[n][2], prop_source_position[n][3]);
      const int source[4] = {prop_source_position[n][0], prop_source_position[n][1], prop_source_position[n][2],
                             prop_source_position[n][3]};

      const int overall_shift_dim = source[corr_dim];

      // FIXME add the smearing too
      for (int i = 0; i < spinor_dim; i++) {
        constructPointSpinorSource(source_array_ptr[i], cs_param.nSpin, cs_param.nColor, inv_param.cpu_prec,
                                   gauge_param.X, i, source);
        inv_param.solver_normalization = QUDA_SOURCE_NORMALIZATION; // Make explicit for now.
        invertQuda(prop_array_ptr_light[i], source_array_ptr[i], &inv_param);
      }
      memset(correlation_function_sum, 0, Nmom*corr_size_in_bytes);
      contractFTQuda(prop_array_ptr, prop_array_ptr_light, &correlation_function_sum, contract_type, &inv_param,
                     (void *)cs_param_ptr, gauge_param.X, source, Mom);
      printfQuda("printing heavy-light correlators\n");
      for (int px=0; px <= Mom[0]; px++) {
        for (int py=0; py <= Mom[1]; py++) {
          for (int pz=0; pz <= Mom[2]; pz++) {
            for (int pt=0; pt <= Mom[3]; pt++ ) {
              for (int G_idx = 0; G_idx < 16; G_idx++) {
                for (size_t t = 0; t < global_corr_length; t++) {
                  int index_real = (px+py*(Mom[0]+1)+pz*(Mom[0]+1)*(Mom[1]+1)+pt*(Mom[0]+1)*(Mom[1]+1)*(Mom[2]+1))
                                   *n_numbers_per_slice * global_corr_length+n_numbers_per_slice * ((t + overall_shift_dim) % global_corr_length) + 2 * G_idx;
                  int index_imag = index_real+1;
                  double sign = G_idx < 8 ? -1. : 1.;
                  printfQuda("sum: prop_n=%d px=%d py=%d pz=%d pt=%d g=%d t=%lu %e %e\n", n, px, py, pz, pt, G_idx, t,
                             ((double *)correlation_function_sum)[index_real]*sign,
                             ((double *)correlation_function_sum)[index_imag]*sign);
                }
              }
            }
          }
        }
      }
    }
    free(prop_array_light);

    //then we calculate heavy-strange correlators
    kappa=kappa_strange;
    setInvertParam(inv_param);
    if (dslash_type == QUDA_CLOVER_WILSON_DSLASH || dslash_type == QUDA_TWISTED_CLOVER_DSLASH) {
      constructHostCloverField(clover, clover_inv, inv_param);
      loadCloverQuda(clover, clover_inv, &inv_param);
    }
    constructWilsonSpinorParam(&cs_param, &inv_param, &gauge_param);
    auto *prop_array_strange = (double *)malloc(spinor_dim * spinor_dim * V * 2 * bytes_per_float);
    void *prop_array_ptr_strange[spinor_dim];
    for (int i = 0; i < spinor_dim; i++) {
      int offset = i * V * spinor_dim * 2;
      prop_array_ptr_strange[i] = prop_array_strange + offset;
    }

    for (int n = 0; n < prop_n_sources; n++) {
      printfQuda("Source position: %d %d %d %d\n", prop_source_position[n][0], prop_source_position[n][1],
                 prop_source_position[n][2], prop_source_position[n][3]);
      const int source[4] = {prop_source_position[n][0], prop_source_position[n][1], prop_source_position[n][2],
                             prop_source_position[n][3]};

      const int overall_shift_dim = source[corr_dim];

      // FIXME add the smearing too
      for (int i = 0; i < spinor_dim; i++) {
        constructPointSpinorSource(source_array_ptr[i], cs_param.nSpin, cs_param.nColor, inv_param.cpu_prec,
                                   gauge_param.X, i, source);
        inv_param.solver_normalization = QUDA_SOURCE_NORMALIZATION; // Make explicit for now.
        invertQuda(prop_array_ptr_strange[i], source_array_ptr[i], &inv_param);
      }
      memset(correlation_function_sum, 0, Nmom*corr_size_in_bytes);
      contractFTQuda(prop_array_ptr, prop_array_ptr_strange, &correlation_function_sum, contract_type, &inv_param,
                     (void *)cs_param_ptr, gauge_param.X, source, Mom);
      printfQuda("printing heavy-strange correlators\n");
      for (int px=0; px <= Mom[0]; px++) {
        for (int py=0; py <= Mom[1]; py++) {
          for (int pz=0; pz <= Mom[2]; pz++) {
            for (int pt=0; pt <= Mom[3]; pt++ ) {
              for (int G_idx = 0; G_idx < 16; G_idx++) {
                for (size_t t = 0; t < global_corr_length; t++) {
                  int index_real = (px+py*(Mom[0]+1)+pz*(Mom[0]+1)*(Mom[1]+1)+pt*(Mom[0]+1)*(Mom[1]+1)*(Mom[2]+1))
                                   *n_numbers_per_slice * global_corr_length+n_numbers_per_slice * ((t + overall_shift_dim) % global_corr_length) + 2 * G_idx;
                  int index_imag = index_real+1;
                  double sign = G_idx < 8 ? -1. : 1.;
                  printfQuda("sum: prop_n=%d px=%d py=%d pz=%d pt=%d g=%d t=%lu %e %e\n", n, px, py, pz, pt, G_idx, t,
                             ((double *)correlation_function_sum)[index_real]*sign,
                             ((double *)correlation_function_sum)[index_imag]*sign);
                }
              }
            }
          }
        }
      }
    }
    free(prop_array_strange);
  }

  if (inv_multigrid) destroyMultigridQuda(mg_preconditioner);

  // free memory
  freeGaugeQuda();
  for (auto &dir : gauge) free(dir);

  if (dslash_type == QUDA_CLOVER_WILSON_DSLASH || dslash_type == QUDA_TWISTED_CLOVER_DSLASH) {
    freeCloverQuda();
    if (clover) free(clover);
    if (clover_inv) free(clover_inv);
  }

  free(source_array);
  free(prop_array);
  free(correlation_function_sum);

  printfQuda("----------------------------------------------------------------------------------\n");
  endQuda();
  finalizeComms();

  return 0;
}
