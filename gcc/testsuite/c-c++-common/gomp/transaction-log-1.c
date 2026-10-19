/* { dg-do compile } */
/* { dg-options "-fopenmp -fdump-tree-omplower" } */
/* { dg-prune-output ".*newtm.*" } */

void
trace (int x)
{
  double y = 0;
  #pragma omp transaction (x, y)
  {
    x++;
    #pragma omp transaction (x)
    { y += x; }
  }
}

/* { dg-final { scan-tree-dump-times "GOMP_transaction_begin \\(\\)" 2 "omplower" } } */
/* { dg-final { scan-tree-dump-times "GOMP_transaction_register \\(" 3 "omplower" } } */
/* { dg-final { scan-tree-dump-times "GOMP_transaction_end \\(\\)" 2 "omplower" } } */
