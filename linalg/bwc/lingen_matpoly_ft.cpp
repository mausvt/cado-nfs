#include "cado.h"
#include <cstdlib>
#include <sstream>
#include "macros.h"
#include "utils.h"
#include "lingen_matpoly_ft.hpp"
#include "lingen_mul_substeps.hpp"
#include "lingen_matpoly_ft.hpp"
#include "logline.h"
#include "sha1.h"

#ifdef HAVE_OPENMP
#include <omp.h>
#endif

#include "lingen_matpoly_bigmatpoly_ft_common.hpp"

template<typename fft_type> struct OP_CTX<matpoly, fft_type> : public OP_CTX_base<matpoly> {
    typedef matpoly T;
    typedef fft_type FFT;
    template<typename... Args>
    OP_CTX(Args&&... args) : OP_CTX_base<T>(args...) {}
    inline int a_irank() const { return 0; }
    inline int b_irank() const { return 0; }
    inline int a_jrank() const { return 0; }
    inline int b_jrank() const { return 0; }
    inline int mesh_inner_size() const { return 1; }
    static const bool uses_mpi = false;
    inline void mesh_checks() const { }
    void alloc_c_if_needed(size_t size) {
        if (c.m != a.m || c.n != a.n || c.capacity() != size)
            c = T(a.ab, a.m, b.n, size);
    }
    inline matpoly const & a_local()const  { return a; }
    inline matpoly const & b_local() const { return b; }
    inline matpoly & c_local() { return c; }
    inline void do_allgather(void *, int) const {}
    inline void begin_smallstep(std::string const &, unsigned int) const { }
    inline void end_smallstep() const {}
    inline void skip_smallstep(std::string const &, unsigned int) const { }
    inline bool local_smallsteps_done() const { return true; }
    template<typename OP> void doit(OP & op, lingen_call_companion::mul_or_mp_times * M) {
        if (M && op.get_transform_ram() > M->per_transform_ram) {
            fprintf(stderr, "Transform size for %s with input operand sizes (%zu, %zu) is %zu, which exceeds expected %zu (anticipated for operand sizes (%zu, %zu). Updating\n",
                    OP::name,
                    a.size,
                    b.size,
                    op.get_transform_ram(),
                    M->per_transform_ram,
                    M->asize,
                    M->bsize
                   );
            size_t ntransforms = M->ram / M->per_transform_ram;
            ASSERT_ALWAYS(M->ram % M->per_transform_ram == 0);
            M->per_transform_ram = op.get_transform_ram();
            M->ram = ntransforms * M->per_transform_ram;
        }
        typename matpoly_ft<fft_type>::memory_guard dummy(M ? M->ram : SIZE_MAX);
        mp_or_mul<OP_CTX<matpoly, fft_type>, OP>(*this, op, M ? & M->S : NULL)();
    }
};

template<typename fft_type> typename matpoly_ft<fft_type>::memory_pool_type matpoly_ft<fft_type>::memory;

template<typename fft_type>
void matpoly_ft<fft_type>::mp_caching_adj(matpoly & c, matpoly const & a, matpoly const & b, unsigned int adj, lingen_call_companion::mul_or_mp_times * M)/*{{{*/
{
    op_mp<fft_type> op(a, b, adj);
    OP_CTX<matpoly, fft_type>(c, a, b).doit(op, M);
} /* }}} */

template<typename fft_type>
void matpoly_ft<fft_type>::mul_caching_adj(matpoly & c, matpoly const & a, matpoly const & b, unsigned int adj, lingen_call_companion::mul_or_mp_times * M)/*{{{*/
{
    op_mul<fft_type> op(a, b, adj);
    OP_CTX<matpoly, fft_type>(c, a, b).doit(op, M);
} /* }}} */

#ifdef SELECT_MPFQ_LAYER_u64k1
template class matpoly_ft<gf2x_fake_fft_info>;
template class matpoly_ft<gf2x_cantor_fft_info>;
template class matpoly_ft<gf2x_ternary_fft_info>;
#else
template class matpoly_ft<fft_transform_info>;
#endif
