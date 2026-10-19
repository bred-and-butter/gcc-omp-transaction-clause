/* { dg-do compile { target c++11 } } */
/* { dg-options "-fopenmp" } */
/* { dg-prune-output ".*newtm.*" } */

void
trace_lambda ()
{
  int x = 0;
  auto work = [&] {
    #pragma omp transaction (x)
    { ++x; }
  };
  work ();
}
