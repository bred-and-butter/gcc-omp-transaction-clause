/* { dg-do run } */
/* { dg-options "-fopenmp" } */
/* { dg-prune-output ".*newtm.*" } */
/* { dg-output ".*newtm_hash_create:.*newtm_hash_acquire:.*inner-done.*newtm_hash_release:.*outer-done.*newtm_hash_destroy:.*" } */

#include <stdio.h>
#include <stdlib.h>

int
main (void)
{
  int x = 0;
  #pragma omp transaction (x, x)
  {
    #pragma omp transaction (x)
    { ++x; }
    puts ("inner-done");
  }
  puts ("outer-done");
  if (x != 1)
    abort ();
  return 0;
}
