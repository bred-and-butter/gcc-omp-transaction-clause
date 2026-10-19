/* { dg-do compile } */

void
newtm_test_clauses (void)
{
  int x = 0, named = 0;
  #pragma omp transaction private (x) /* { dg-error "does not accept clauses" } */
  { x++; }
  #pragma omp transaction hint (0) /* { dg-error "does not accept clauses" } */
  { x++; }
  #pragma omp transaction (named)
  { x++; }
  #pragma omp transaction
  { x++; }
}
