/* { dg-do compile } */
/* { dg-options "-fopenmp" } */
/* { dg-prune-output ".*newtm.*" } */

void function (void);
typedef int alias;
void
invalid_objects (void)
{
  #pragma omp transaction (missing) /* { dg-error "is not a variable" } */
  {}
  #pragma omp transaction (function) /* { dg-error "is not a variable" } */
  {}
  #pragma omp transaction (alias) /* { dg-error "is not a variable" } */
  {}
}
