#ifndef LINGEN_MATPOLY_H_
#define LINGEN_MATPOLY_H_

#include <mutex>
#include <list>
#include <tuple>
#include <functional>   /* reference_wrapper */

#include "mpfq_layer.h"

class matpoly;
struct polymat;

#include "lingen-polymat.hpp"

struct submatrix_range {
    unsigned int i0=0,j0=0;
    unsigned int i1=0,j1=0;
    unsigned int nrows() const { return i1-i0; }
    unsigned int ncols() const { return j1-j0; }
    submatrix_range & range() { return *this; }
    submatrix_range const & range() const { return *this; }
    submatrix_range() = default;
    submatrix_range(unsigned int i0, unsigned int j0, unsigned int ni, unsigned int nj) : i0(i0), j0(j0), i1(i0+ni), j1(j0+nj) {}
    template<typename T>
    submatrix_range(T const & M) : i0(0), j0(0), i1(M.nrows()), j1(M.ncols()) {}
    template<typename T>
    inline bool valid(T const & a) const {
        return i0 <= i1 && i1 <= a.m && j0 <= j1 && j1 <= a.n;
    }
    submatrix_range operator*(submatrix_range const & a) const {
        ASSERT_ALWAYS(ncols() == a.nrows());
        return submatrix_range(i0, nrows(), a.j0, a.ncols());
    }
};

/* This is used only for plingen. */

/* We use abvec because this offers the possibility of having flat data
 *
 * Note that this ends up being exactly the same data type as polymat.
 * The difference here is that the stride is not the same.
 */

/* the "stack" allocation model is meant to alleviate the problem of
 * memory fragmentation. As it turns out, it's actually ineffective.
 */
#define USE_STACK_ALLOCATION_MODEL

class matpoly {
    struct memory_pool {
        class layer {
            std::mutex mm;
#ifdef USE_STACK_ALLOCATION_MODEL
            void * top = nullptr;     /* next area to allocate */
            void * base = nullptr;    /* base of the allocation */
            size_t allowed_coarse=0;
            size_t allocated_coarse=0;
            /* We have
             *      base <= top
             *      top = base + allocated_coarse
             *      top <= base + allowed_coarse
             *
             * And as for the user-visible counts:
             *      allowed <= allowed_coarse
             *      allocated <= allocated_coarse
             *
             * "coarse" allocation typically includes pagesize rounding + red
             * area.
             */
            bool is_stack_based() const { return base != NULL; }
#else
            bool is_stack_based() const { return false; }
#endif
            public:
            bool is_on_top(const void * p, size_t s) const;
            size_t allowed=0;
            size_t allocated=0;
            size_t peak=0;
            size_t max_inaccuracy = 0;
            /* This takes a list of pointers (and sizes of pointed-to areas)
             * that are on top of the stack (in order: the last one must
             * really be on top), and insert a new hole just before them (of
             * the requested size).
             * The pointers areas are moved, and the pointers are modified
             * accordingly.
             *
             * This is implemented as a simple alloc() call when
             * USE_STACK_ALLOCATION_MODEL is not defined.
             */
            void * insert_high(size_t, std::list<std::pair<void *, size_t>> & pointers);
            void roll(std::list<std::pair<void *, size_t>> & pointers);
            void * alloc(size_t);
            void free(void *, size_t);
            void * realloc(void * p, size_t s0, size_t s);
            void report_inaccuracy(ssize_t diff);
            layer(size_t, bool stack_based=false);
            ~layer();
        };
        std::mutex mm;
        std::list<memory_pool::layer> layers;
        inline void * insert_high(size_t s, std::list<std::pair<void *, size_t>> & p) {
            ASSERT_ALWAYS(!layers.empty());
            return layers.back().insert_high(s, p);
        }
        inline void roll(std::list<std::pair<void *, size_t>> & p) {
            ASSERT_ALWAYS(!layers.empty());
            layers.back().roll(p);
        }
        void * alloc(size_t s) {
            ASSERT_ALWAYS(!layers.empty());
            return layers.back().alloc(s);
        }
        void free(void * p, size_t s) {
            ASSERT_ALWAYS(!layers.empty());
            layers.back().free(p, s);
        }
        void * realloc(void * p, size_t s0, size_t s) {
            ASSERT_ALWAYS(!layers.empty());
            return layers.back().realloc(p, s0, s);
        }
        bool is_on_top(const void * p, size_t s) {
            ASSERT_ALWAYS(!layers.empty());
            return layers.back().is_on_top(p, s);
        }

    };
#if 0
    struct memory_pool {
        struct layer {
            std::mutex mm;
            size_t allowed=0;
            ssize_t allocated=0;
            ssize_t peak=0;
            layer(size_t s) : allowed(s) { allocated=peak=0; }
            void * alloc(memory_pool& M, size_t);
            void free(memory_pool& M, void *, size_t);
            void *realloc(memory_pool& M, void *, size_t, size_t);
        };
        std::mutex mm;
        std::list<memory_pool::layer> layers;
        void * alloc(size_t);
        void free(void *, size_t);
        void *realloc(void *, size_t, size_t);
        layer & current_pool();
        size_t max_inaccuracy = 0;
        void report_inaccuracy(ssize_t diff);
    };
#endif
    static memory_pool memory;
public:
    class memory_pool_guard {
        size_t s;
        public:
        memory_pool_guard(size_t s, bool stack_based=false);
        ~memory_pool_guard();
    };
    // static void add_to_main_memory_pool(size_t s);
    abdst_field ab = NULL;
    unsigned int m = 0;
    unsigned int n = 0;
    size_t size = 0;
    size_t alloc = 0;
    abvec x = NULL;
    inline unsigned int nrows() const { return m; }
    inline unsigned int ncols() const { return n; }
    size_t alloc_size() const;

    matpoly() { m=n=0; size=alloc=0; ab=NULL; x=NULL; }
    matpoly(abdst_field ab, unsigned int m, unsigned int n, int len,
            std::list<std::reference_wrapper<matpoly>> = {}
            );
    static void roll(std::list<std::reference_wrapper<matpoly>>);
    bool is_on_top() const {
        return memory.is_on_top(x, alloc_size());
    }
    matpoly(matpoly const&) = delete;
    matpoly& operator=(matpoly const&) = delete;
    matpoly(matpoly &&);
    matpoly& operator=(matpoly &&);
    ~matpoly();
    int check_pre_init() const;
    void realloc(size_t);
    inline void shrink_to_fit() { realloc(size); }
    void zero();
    /* {{{ access interface for matpoly */
    inline abdst_vec part(unsigned int i, unsigned int j, unsigned int k=0) {
        ASSERT_ALWAYS(size);
        return abvec_subvec(ab, x, (i*n+j)*alloc+k);
    }
    inline abdst_elt coeff(unsigned int i, unsigned int j, unsigned int k=0) {
        return abvec_coeff_ptr(ab, part(i,j,k), 0);
    }
    inline absrc_vec part(unsigned int i, unsigned int j, unsigned int k=0) const {
        ASSERT_ALWAYS(size);
        return abvec_subvec_const(ab, x, (i*n+j)*alloc+k);
    }
    inline absrc_elt coeff(unsigned int i, unsigned int j, unsigned int k=0) const {
        return abvec_coeff_ptr_const(ab, part(i,j,k), 0);
    }
    /* }}} */
    void set_constant_ui(unsigned long e);
    void set_constant(absrc_elt e);
#if 0
    void swap(matpoly&);
#endif
    void fill_random(unsigned int size, gmp_randstate_t rstate);
    int cmp(matpoly const & b) const;
    void multiply_column_by_x(unsigned int j, unsigned int size);
    void divide_column_by_x(unsigned int j, unsigned int size);
    void truncate(matpoly const & src, unsigned int size);
    void extract_column(
        unsigned int jdst, unsigned int kdst,
        matpoly const & src, unsigned int jsrc, unsigned int ksrc);
    void transpose_dumb(matpoly const & src);
    void zero_column(unsigned int jdst, unsigned int kdst);
    void extract_row_fragment(unsigned int i1, unsigned int j1,
        matpoly const & src, unsigned int i0, unsigned int j0,
        unsigned int n);
    void rshift(matpoly const &, unsigned int k);
    void addmul(matpoly const & a, matpoly const & b);
    void mul(matpoly const & a, matpoly const & b);
    void addmp(matpoly const & a, matpoly const & c);
    void mp(matpoly const & a, matpoly const & c);
    void set_polymat(polymat const & src);
    int coeff_is_zero(unsigned int k) const;
    void coeff_set_zero(unsigned int k);
    struct view_t;
    struct const_view_t;

    struct view_t : public submatrix_range {
        matpoly & M;
        view_t(matpoly & M, submatrix_range S) : submatrix_range(S), M(M) {}
        view_t(matpoly & M) : submatrix_range(M), M(M) {}
        inline abdst_vec part(unsigned int i, unsigned int j) {
            return M.part(i0+i, j0+j);
        }
        inline absrc_vec part(unsigned int i, unsigned int j) const {
            return M.part(i0+i, j0+j);
        }
    };

    struct const_view_t : public submatrix_range {
        matpoly const & M;
        const_view_t(matpoly const & M, submatrix_range S) : submatrix_range(S), M(M) {}
        const_view_t(matpoly const & M) : submatrix_range(M), M(M) {}
        const_view_t(view_t const & V) : submatrix_range(V), M(V.M) {}
        inline absrc_vec part(unsigned int i, unsigned int j) const {
            return M.part(i0+i, j0+j);
        }
    };
    view_t view(submatrix_range S) { ASSERT_ALWAYS(S.valid(*this)); return view_t(*this, S); }
    const_view_t view(submatrix_range S) const { ASSERT_ALWAYS(S.valid(*this)); return const_view_t(*this, S); }
    view_t view() { return view_t(*this); }
    const_view_t view() const { return const_view_t(*this); }
    matpoly truncate_and_rshift(unsigned int truncated_size, unsigned int rshift);
};

#endif	/* LINGEN_MATPOLY_H_ */
