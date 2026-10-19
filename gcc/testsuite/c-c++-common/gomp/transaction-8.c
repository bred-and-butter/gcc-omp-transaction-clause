/* { dg-do run } */
/* { dg-options "-fopenmp -O2 -fdump-tree-gimple -fdump-tree-omplower" } */

int
main (void)
{
  int value = 0;
  _Pragma ("omp transaction")
  {
    value++;
  }
  #pragma omp critical
  {
    #pragma omp transaction
    {
      value += 2;
    }
  }
  #pragma omp transaction
  {
    #pragma omp critical (newtm_lock)
    {
      value += 4;
    }
  }
  if (value != 7)
    __builtin_abort ();
  return 0;
}

/* { dg-final { scan-tree-dump-times "#pragma omp transaction" 3 "gimple" } } */
/* { dg-final { scan-tree-dump-times "GOMP_critical_start" 1 "omplower" } } */
/* { dg-final { scan-tree-dump-times "GOMP_critical_end" 1 "omplower" } } */
/* { dg-final { scan-tree-dump-times "GOMP_critical_name_start" 1 "omplower" } } */
/* { dg-final { scan-tree-dump-times "GOMP_critical_name_end" 1 "omplower" } } */
