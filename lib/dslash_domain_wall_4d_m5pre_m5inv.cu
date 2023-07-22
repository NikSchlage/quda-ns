#include <dslash_domain_wall_4d_fused_m5.hpp>

/**
   This is the gauged domain-wall 4-d preconditioned operator, fused with immediately followed fifth dimension operators.
*/

namespace quda
{

  // Apply the 4-d preconditioned domain-wall Dslash operator
  //   i.e. out(x) = M*in = in(x) + a*\sum_mu U_{-\mu}(x)in(x+mu) + U^\dagger_mu(x-mu)in(x-mu)
  // ... and then m5pre + m5inv
#ifdef GPU_DOMAIN_WALL_DIRAC
  void ApplyDomainWall4DM5preM5inv(ColorSpinorField &out, const ColorSpinorField &in, const GaugeField &U, real_t a,
                                   real_t m_5, const complex_t *b_5, const complex_t *c_5, const ColorSpinorField &x,
                                   ColorSpinorField &y, int parity, bool dagger, const int *comm_override, real_t m_f,
                                   TimeProfile &profile)
  {
    auto dummy_list = Dslash5TypeList<Dslash5Type::M5_PRE_MOBIUS_M5_INV>();
    instantiate<DomainWall4DApplyFusedM5>(out, in, U, a, m_5, b_5, c_5, x, y, parity, dagger, comm_override, m_f,
                                          dummy_list, profile);
  }
#else
  void ApplyDomainWall4DM5preM5inv(ColorSpinorField &, const ColorSpinorField &, const GaugeField &, real_t,
                                   real_t, const complex_t *, const complex_t *, const ColorSpinorField &,
                                   ColorSpinorField &, int, bool, const int *, real_t, TimeProfile &)
  {
    errorQuda("Domain-wall dslash has not been built");
  }
#endif // GPU_DOMAIN_WALL_DIRAC

} // namespace quda
