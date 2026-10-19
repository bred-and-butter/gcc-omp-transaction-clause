/* { dg-do compile } */

#pragma omp transaction /* { dg-error "expected declaration|expected unqualified-id" } */
int newtm_invalid_file_scope;

void
newtm_missing_body (void)
{
  #pragma omp transaction
} /* { dg-error "expected" } */
