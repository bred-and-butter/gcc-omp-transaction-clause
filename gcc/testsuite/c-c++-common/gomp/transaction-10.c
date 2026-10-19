/* { dg-do compile } */
/* { dg-options "-fopenmp" } */

void
newtm_test_identifier_errors (void)
{
  int a = 0, b = 0;
  #pragma omp transaction () /* { dg-error "expected identifier" } */
  { }
  #pragma omp transaction (a,) /* { dg-error "expected identifier" } */
  { }
  #pragma omp transaction (,a) /* { dg-error "expected identifier" } */
  { }
  #pragma omp transaction (1) /* { dg-error "expected identifier" } */
  { }
  #pragma omp transaction (a b) /* { dg-error "expected '\\)" } */
  { }
  #pragma omp transaction (a /* { dg-error "expected '\\)" } */
  { }
  #pragma omp transaction (a + b) /* { dg-error "expected '\\)" } */
  { }
  #pragma omp transaction (a) private (a) /* { dg-error "does not accept clauses" } */
  { }
  #pragma omp transaction (a, b)
  { }
}
