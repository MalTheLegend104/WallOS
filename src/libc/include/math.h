#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

	double         fmod(double x, double y);
	double 		   modf(double x, double* iptr);
	double         floor(double arg);
	float          floorf(float arg);
	long double    floorl(long double arg);
	double 		   cos(double theta);
	double 		   sin(double theta);
	int 		   abs(int i);

#ifdef __cplusplus
}
#endif

#endif