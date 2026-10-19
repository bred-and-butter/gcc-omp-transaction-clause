/* { dg-do run } */
/* { dg-options "-fopenmp -fdump-tree-gimple -fdump-tree-omplower" } */

int
main (void)
{
  int result = 0;
  #pragma omp transaction
  {
    int local = 2;
    for (int i = 0; i < 4; ++i)
      if (i & 1)
	result += local;
    #pragma omp transaction
    {
      result *= 3;
    }
  }
  #pragma omp transaction
  {
  }
  if (result != 12)
    __builtin_abort ();
  return 0;
}

/* { dg-final { scan-tree-dump-times "#pragma omp transaction" 3 "gimple" } } */
/* { dg-final { scan-tree-dump-not "omp transaction" "omplower" } } */
/* { dg-final { scan-tree-dump-not "GOMP_critical|_ITM_|omp critical" "omplower" } } */
