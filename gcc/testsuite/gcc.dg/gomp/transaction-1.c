/* { dg-do run } */
/* { dg-options "-fopenmp -O2" } */

int
main (void)
{
  int value = 3;
  int newtm_nested (int n)
  {
    #pragma omp transaction
    {
      value += n;
    }
    return value;
  }
  #pragma omp transaction
  {
    if (newtm_nested (4) != 7)
      __builtin_abort ();
  }
  return 0;
}
