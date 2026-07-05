#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>



typedef double real_t;

static int saxpy_verify(const real_t *y,double AVAL,double XVAL, double YVAL)
{
    float err = 0.0;
    for (size_t i = 0; i < N; ++i)
	err = err + fabs(y[i] - (AVAL * XVAL + YVAL));

    printf("Errors: %f\n",err);
    return err == 0.0;
}
