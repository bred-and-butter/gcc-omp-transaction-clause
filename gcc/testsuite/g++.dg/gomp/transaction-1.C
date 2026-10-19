/* { dg-do run } */
/* { dg-options "-fopenmp -O2" } */

struct newtm_guard
{
  int &value;
  newtm_guard (int &v) : value (v) { ++value; }
  ~newtm_guard () { value += 10; }
};

template <int N>
int
newtm_template ()
{
  int value = 0;
  #pragma omp transaction
  {
    newtm_guard guard (value);
    #pragma omp transaction
    {
      value += N;
    }
  }
  return value;
}

int
main ()
{
  if (newtm_template<5> () != 16)
    __builtin_abort ();
  return 0;
}
