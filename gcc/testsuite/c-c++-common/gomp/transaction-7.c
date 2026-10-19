/* { dg-do compile } */

void
newtm_test_critical_nesting (void)
{
  #pragma omp critical (lock)
  {
    #pragma omp transaction
    {
      #pragma omp critical (lock) /* { dg-error "may not be nested" } */
      { }
    }
  }
}
