/* { dg-do compile } */
/* { dg-options "-fopenmp -fdump-tree-gimple-raw -fdump-tree-omplower" } */

void
newtm_test_representation (int *value)
{
  #pragma omp transaction
  {
    *value += 1;
  }
}

/* The raw name comes from the independent GIMPLE_OMP_TRANSACTION code.  */
/* { dg-final { scan-tree-dump "gimple_omp_transaction" "gimple" } } */
/* { dg-final { scan-tree-dump-not "gimple_omp_critical|gimple_transaction" "gimple" } } */
/* { dg-final { scan-tree-dump-not "GOMP_critical|_ITM_" "omplower" } } */
/* { dg-final { scan-assembler-not "GOMP_critical|_ITM_" } } */

/* Tracing introduces calls without aliasing critical or GNU TM.  */
/* { dg-final { scan-tree-dump "GOMP_transaction_begin" "omplower" } } */
/* { dg-final { scan-tree-dump "GOMP_transaction_end" "omplower" } } */
