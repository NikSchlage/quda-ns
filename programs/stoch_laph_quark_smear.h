#pragma once

#include <quda.h>
#include <quda_internal.h>
#include <dirac_quda.h>
#include <dslash_quda.h>
#include <invert_quda.h>
#include <util_quda.h>
#include <blas_quda.h>

#include <command_line_params.h>
#include <host_utils.h>
#include <misc.h>

using namespace quda;

void display_driver_info();
void evecProjectQuda(const ColorSpinorField &x, const ColorSpinorField &y, void *result);
void spinDiluteQuda(ColorSpinorField &x, ColorSpinorField &y, const int alpha);
void laphSourceConstruct(std::vector<ColorSpinorField*> &quarks, std::vector<ColorSpinorField*> &evecs, const Complex *noise, const int dil_scheme);
void laphSourceInvert(std::vector<ColorSpinorField*> &quarks, QudaInvertParam *inv_param, const int *X);
void laphSinkProject(std::vector<ColorSpinorField*> &quarks, std::vector<ColorSpinorField*> &evecs,
		     void **host_sinks, const int dil_scheme);
