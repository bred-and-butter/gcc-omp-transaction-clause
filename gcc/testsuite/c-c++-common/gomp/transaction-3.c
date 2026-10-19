/* { dg-do run } */
/* { dg-options "-fopenmp" } */

int
main (void)
{
  int result = 0;
  int participants = 0;
  #pragma omp parallel reduction (+:result, participants)
  {
    #pragma omp transaction
    {
      int local = 7;
      #pragma omp transaction
      {
	local += 2;
      }
      /* Each thread executes its body with the enclosing reduction context.  */
      result += local;
      participants++;
    }
    #pragma omp transaction
    {
      #pragma omp for
      for (int i = 0; i < 100; ++i)
	result += 1;
    }
  }
  if (participants < 1 || result != 100 + 9 * participants)
    __builtin_abort ();
  return 0;
}
