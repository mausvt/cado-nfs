#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE     /* for MMAP_ANONYMOUS when using electric_alloc */

#include "xdotprod.h"
#include "manu.h"

#include "bw-common.h"


void x_dotprod(matmul_top_data_ptr mmt, uint32_t * xv, abt * v)
{
    /* We're reading from the shared right vector data -- this area is
     * written to by the other threads in the column. Some of them might
     * be lingering in reduce operations, so we have to wait for them
     */
    if (mmt->wr[bw->dir]->v->flags & THREAD_SHARED_VECTOR) {
        serialize_threads(mmt->pi->wr[bw->dir]);
    } else {
        /* I presume that no locking is needed here. But it's unchecked
         */
        BUG();
    }

    for(int j = 0 ; j < bw->m ; j++) {
        abt * where = v + aboffset(mmt->abase, j);
        for(unsigned int t = 0 ; t < bw->nx ; t++) {
            uint32_t i = xv[j*bw->nx+t];
            unsigned int vi0 = mmt->wr[bw->dir]->i0;
            unsigned int vi1 = mmt->wr[bw->dir]->i1;
            unsigned int hi0 = mmt->wr[!bw->dir]->i0;
            unsigned int hi1 = mmt->wr[!bw->dir]->i1;
            if (i < vi0 || i >= vi1)
                continue;
            /* We want the data to match our interval on both
             * directions, because otherwise we'll end up
             * computing rubbish -- recall that no broadcast_down
             * has occurred yet.
             */
            if (i < hi0 || i >= hi1)
                continue;
            abadd(mmt->abase, where,
                    mmt->wr[bw->dir]->v->v + aboffset(mmt->abase, i - vi0));
        }
    }
}

