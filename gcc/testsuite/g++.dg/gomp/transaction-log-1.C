/* { dg-do compile } */
/* { dg-options "-fopenmp -fdump-tree-omplower" } */
/* { dg-prune-output ".*newtm.*" } */

template<typename T>
void trace (T &object)
{
  #pragma omp transaction (object)
  { ++object; }
}

void instantiate (int &x) { trace (x); }

/* { dg-final { scan-tree-dump "GOMP_transaction_register \\(object," "omplower" } } */
