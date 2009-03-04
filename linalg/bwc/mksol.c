#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE

#include <stdio.h>

#include "parallelizing_info.h"
#include "matmul_top.h"
#include "abase.h"
#include "select_mpi.h"
#include "manu.h"
#include "debug.h"
#include "params.h"
#include "xvectors.h"
#include "xymats.h"
#include "bw-common-mpi.h"
#include "async.h"
// #include "rusage.h"
#include "filenames.h"
#include "xdotprod.h"

#include "electric_alloc.h"     // debugging

abobj_t abase;

void * mksol_prog(parallelizing_info_ptr pi, void * arg MAYBE_UNUSED)
{
    int tcan_print = bw->can_print && pi->m->trank == 0;
    matmul_top_data mmt;
    struct timing_data timing[1];

    int flags[2];
    flags[bw->dir] = THREAD_SHARED_VECTOR;
    flags[!bw->dir] = 0;

    block_control_signals();

    matmul_top_init(mmt, abase, pi, flags, bw->matrix_filename);

    mmt_wiring_ptr mcol = mmt->wr[bw->dir];
    mmt_wiring_ptr mrow = mmt->wr[!bw->dir];
    pi_wiring_ptr picol = mmt->pi->wr[bw->dir];
    // pi_wiring_ptr pirow = mmt->pi->wr[!bw->dir];

    serialize(pi->m);
    
    abzero(abase, mrow->v->v, mrow->i1 - mrow->i0);

    uint32_t * gxvecs = malloc(bw->nx * bw->m * sizeof(uint32_t));

    load_x(gxvecs, bw->m, bw->nx, pi);

    int rc;

    char * v_name;
    rc = asprintf(&v_name, V_FILE_BASE_PATTERN, bw->ys[0], bw->ys[1]);
    char * s_name;
    rc = asprintf(&s_name, S_FILE_BASE_PATTERN, bw->ys[0], bw->ys[1]);

    if (tcan_print) { printf("Loading %s...", v_name); fflush(stdout); }
    matmul_top_load_vector(mmt, v_name, bw->dir, bw->start);
    if (tcan_print) { printf("done\n"); }

    // We must also fetch the check vector C.
    abvobj_t abase_check;
    abvobj_set_nbys(abase_check, NCHECKS_CHECK_VECTOR);

    size_t vstride = abvbytes(abase_check, 1);
    size_t stride =  abbytes(abase, 1);

    mmt_generic_vec check_vector;
    matmul_top_vec_init_generic(mmt, abvbytes(abase_check, 1),
            check_vector, !bw->dir, THREAD_SHARED_VECTOR);
    if (tcan_print) { printf("Loading check vector..."); fflush(stdout); }
    matmul_top_load_vector_generic(mmt, vstride,
            check_vector, CHECK_FILE_BASE, !bw->dir, bw->interval);
    if (tcan_print) { printf("done\n"); }

    /* F plays the role of a right-hand-side in mksol. Vector iterates
     * have to be multiplied on the right by a matrix corresponding to
     * some coefficient of F. Because F has been written to disk in the
     * proper order (=reversed order, leading coeff first) by lingen,
     * iterate i is to be multiplied by the i-th coefficient of F.
     *
     * The F-coefficients, from the overall description of the
     * algorithm, are square matrices having bw->n rows and columns.
     * Columns correspond to candidate solutions. Rows correspond to the
     * fact that contributions from all vector iterates have to be added
     * in order to form a candidate solution. However, for
     * the purpose of splitting the work across sites, it's stored on
     * disk in the transposed order, so that the already existing split
     * program is able to do the split. This means that the data we read
     * here consists of matrices having unconditionally bw->n rows, and
     * ys[1]-ys[0] columns. Because our multiply-by-smallish-matrix code
     * does not like this ordering, we have to transpose again in order
     * to obtain a matrix with ys[1]-ys[0] rows, and bw->n columns. This
     * matrix is called rhs in the text below.
     */
    abvobj_t abase_rhs;
    abvobj_set_nbys(abase_rhs, bw->n);

    size_t rstride = abvbytes(abase_rhs, 1);

    ASSERT(bw->n * stride == rstride*abnbits(abase));

    /* Our ``ahead zone'' will be used for storing the data prior to
     * transposing */
    mmt_vec ahead;
    vec_init_generic(pi->m, stride, (mmt_generic_vec_ptr) ahead, 0, bw->n);


    /* We'll load all the F coefficient matrices before the main loop.
     * It's dual to the treatment of the xy matrices in krylov. Same
     * considerations apply w.r.t the memory footprint.
     */
    mmt_generic_vec fcoeffs;
    if (tcan_print) {
        printf("Each thread allocates %zu kb for the F matrices\n",
                abvbytes(abase_rhs, abnbits(abase)*bw->interval) >> 10);
    }
    vec_init_generic(pi->m, rstride, (mmt_generic_vec_ptr) fcoeffs, 0, abnbits(abase)*bw->interval);
    
    /* Our sum vector is only part of the story of course. All jobs, all
     * threads, will participate to the computation of the products by
     * tiny matrices. When dir==1, within a column, all threads have
     * access to the mcol->i0..mcol->i1 interval (which is roughly a
     * fraction equal to 1/(pirow->njobs * pirow->ncores) of the total
     * vector size. Now this interval will be split in (picol->njobs *
     * picol->ncores) parts.
     */

    unsigned int ii0, ii1;
    unsigned int di = mcol->i1 - mcol->i0;

    ii0 = mcol->i0 + di * (picol->jrank * picol->ncores + picol->trank) /
        (picol->njobs * picol->ncores);
    ii1 = mcol->i0 + di * (picol->jrank * picol->ncores + picol->trank + 1) /
        (picol->njobs * picol->ncores);

    mmt_generic_vec sum;
    vec_init_generic(mmt->pi->m, rstride, sum, 0, ii1-ii0);

    timing_init(timing, bw->start);

    if (bw->end == 0) {
        // for mksol we use EOF as an ending indication.
        serialize_threads(pi->m);
        bw->end = INT_MAX;
        if (tcan_print) {
            fprintf(stderr, "Target iteration is unspecified ;"
                    " going to end of F file\n");
        }
    } else if (tcan_print) {
        fprintf(stderr, "Target iteration is %u ; going to %u\n", bw->end,
                bw->interval * iceildiv(bw->end, bw->interval));
    }

    for(int s = bw->start ; s < bw->end ; s += bw->interval ) {
        serialize(pi->m);
        if (pi->m->trank == 0 && pi->m->jrank == 0) {
            abase_generic_zero(rstride, fcoeffs->v, abnbits(abase)*bw->interval);
            char * tmp;
            rc = asprintf(&tmp, F_FILE_SLICE_PATTERN, bw->ys[0], bw->ys[1]);

            FILE * f = fopen(tmp, "r");
            rc = fseek(f, bw->n * stride * s, SEEK_SET);
            if (rc < 0) {
                bw->end = s;
            }

            for(int i = 0 ; i < bw->interval ; i++) {
                rc = fread(ahead->v, stride, bw->n, f);
                BUG_ON(rc != 0 && rc != bw->n);
                if (rc == 0) {
                    printf("Exhausted F data after %d coefficients\n", s+i);
                    bw->end = s+i;
                    break;
                }
                size_t off = i * abnbits(abase) * rstride;
                abase_generic_ptr fptr = abase_generic_ptr_add(fcoeffs->v, off);

#if 0
                {
                    void * test;
                    test = electric_alloc(rstride * abnbits(abase));
                    abvtranspose(abase_rhs, abase, test, ahead->v);
                    electric_free(test,rstride * abnbits(abase));
                }
#endif
                abvtranspose(abase_rhs, abase, fptr, ahead->v);
            }
            fclose(f);
            free(tmp);
        }
        if (pi->m->trank == 0) {
            /* we're good friends with the global leader. Try to grasp
             * the information about the end value -- it might have
             * changed because of EOF. We're not so disturbed
             * by serialization points here. */
            MPI_Bcast(&(bw->end), 1, MPI_INT, 0, pi->m->pals);
        }
        serialize_threads(pi->m);

        /* broadcast f */
        size_t siz = abnbits(abase) * rstride * bw->interval;
        broadcast_generic(fcoeffs, pi->m, siz, 0, 0);

        abase_generic_zero(rstride, sum->v, ii1 - ii0);

        // Plan ahead. The check vector is here to predict the final A matrix.
        // Our share of the dot product is determined by the
        // intersections of the i0..i1 intervals on both sides.
        
        unsigned int how_many;
        unsigned int offset_c;
        unsigned int offset_v;
        how_many = intersect_two_intervals(&offset_c, &offset_v,
                mrow->i0, mrow->i1,
                mcol->i0, mcol->i1);

        abzero(abase, ahead->v, NCHECKS_CHECK_VECTOR);
        if (how_many) {
            size_t bytes_c =  abvbytes(abase_check, offset_c);
            abvt * c = abase_generic_ptr_add(check_vector->v, bytes_c);
            abt * v = mcol->v->v + aboffset(abase, offset_v);
            abvdotprod(abase, abase_check, ahead->v, c, v, how_many);
        }

        serialize(pi->m);

        /* Despite the fact that the bw->end value might lead us
         * to stop earlier, we want to stop at multiples of the checking
         * interval. That's important, otherwise the last bunch of
         * computations won't be checked.
         */
        for(int i = 0 ; i < bw->interval ; i++) {
            size_t off = i * abnbits(abase) * rstride;
            abase_generic_ptr fptr = abase_generic_ptr_add(fcoeffs->v, off);

            abvaddmul_tiny(abase, abase_rhs,
                    sum->v,
                    ((abvt*) mcol->v->v) + abvoffset(abase_rhs, ii0 - mcol->i0),
                    fptr, ii1 - ii0);

            matmul_top_mul(mmt, bw->dir);

            timing_check(pi, timing, s+i+1, tcan_print);
        }
        serialize(pi->m);

        /* Last dot product. This must cancel ! Recall that x_dotprod
         * adds to the result. */
        x_dotprod(mmt, gxvecs, ahead->v);

        allreduce_generic(abase, ahead, pi->m, NCHECKS_CHECK_VECTOR);
        if (!abis_zero(abase, ahead->v, NCHECKS_CHECK_VECTOR)) {
            printf("Failed check at iteration %d\n", s + bw->interval);
            exit(1);
        }

        char * s_name2;
        rc = asprintf(&s_name2, COMMON_VECTOR_ITERATE_PATTERN, s_name, s + bw->interval);
        pi_save_file_2d(pi, bw->dir, s_name2, sum->v, (ii1 - ii0) * rstride);
        free(s_name2);

        matmul_top_save_vector(mmt, v_name, bw->dir, s + bw->interval);

        serialize(pi->m);

        // reached s + bw->interval. Count our time on cpu, and compute the sum.
        timing_disp_collective_oneline(pi, timing, s + bw->interval, tcan_print);
    }

    if (tcan_print) {
        printf("Done.\n");
    }
    serialize(pi->m);

    matmul_top_vec_clear_generic(mmt, abvbytes(abase_check, 1), check_vector, !bw->dir);
    matmul_top_vec_clear_generic(mmt, rstride, (mmt_generic_vec_ptr) sum, bw->dir);
    vec_clear_generic(pi->m, stride, (mmt_generic_vec_ptr) ahead, NCHECKS_CHECK_VECTOR);
    vec_clear_generic(pi->m, rstride, (mmt_generic_vec_ptr) fcoeffs, abnbits(abase)*bw->interval);
    free(gxvecs);
    free(v_name);
    free(s_name);

    matmul_top_clear(mmt, abase);

    timing_clear(timing);

    return NULL;
}


void usage()
{
    fprintf(stderr, "Usage: ./mksol <options>\n");
    fprintf(stderr, bw_common_usage_string());
    fprintf(stderr, "Relevant options here: wdir cfg m n mpi thr matrix interval start ys\n");
    fprintf(stderr, "Note: data files must be found in wdir !\n");
    exit(1);
}

int main(int argc, char * argv[])
{
    param_list pl;
    param_list_init(pl);
    bw_common_init_mpi(bw, pl, argc, argv);
    if (param_list_warn_unused(pl)) usage();
    param_list_clear(pl);

    if (bw->nx == 0) { fprintf(stderr, "no nx value set\n"); exit(1); } 
    if (bw->ys[0] < 0) { fprintf(stderr, "no ys value set\n"); exit(1); }

    abobj_init(abase);
    abobj_set_nbys(abase, bw->ys[1]-bw->ys[0]);

    catch_control_signals();
    pi_go(mksol_prog, bw->mpi_split[0], bw->mpi_split[1], bw->thr_split[0], bw->thr_split[1], 0);

    bw_common_clear_mpi(bw);
    return 0;
}

