/* { dg-do compile } */

void
newtm_test_exit (void)
{
  #pragma omp transaction
  {
    goto outside; /* { dg-error "invalid branch" } */
    /* { dg-message "from here|exits OpenMP" "" { target c++ } .-1 } */
  }
outside:; /* { dg-error "jump to label" "" { target c++ } } */
  /* { dg-message "exits OpenMP" "" { target c++ } .-1 } */
}

void
newtm_test_entry (void)
{
  goto inside; /* { dg-error "invalid entry" } */
  /* { dg-message "from here" "" { target c++ } .-1 } */
  #pragma omp transaction
  {
inside:; /* { dg-error "jump to label" "" { target c++ } } */
    /* { dg-message "enters OpenMP" "" { target c++ } .-1 } */
  }
}
