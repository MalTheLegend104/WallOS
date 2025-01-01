#include <math.h>

#if defined(__x86_64__)

inline double sin(double theta) {
	double result;
	__asm__(
		"fsin\n"                 // Use x87 FPU instruction for sine
		: "=t" (result)          // Output: result in the FPU stack top
		: "0" (theta)            // Input: theta in FPU stack top
	);
	return result;
}

// Function to calculate cosine using x87 FPU
inline double cos(double theta) {
	double result;
	__asm__(
		"fcos\n"                 // Use x87 FPU instruction for cosine
		: "=t" (result)          // Output: result in the FPU stack top
		: "0" (theta)            // Input: theta in FPU stack top
	);
	return result;
}
#else

// Almost every processor that has a dedicated floating point unit can do sin/cos natively.
// Cordic works on systems that have no floating point numbers at all.
// This is the fallback for non x86_64 systems.

#define CORDIC_ITERATIONS 16

// Precomputed arctangent values in radians (atan(2^-i))
static const double cordic_atan_table[CORDIC_ITERATIONS] = {
	0.7853981633974483,  // atan(2^0)
	0.4636476090008061,  // atan(2^-1)
	0.2449786631268641,  // atan(2^-2)
	0.1243549945467614,  // atan(2^-3)
	0.0624188099959574,  // atan(2^-4)
	0.0312398334302683,  // atan(2^-5)
	0.0156237286204768,  // atan(2^-6)
	0.0078123410601011,  // atan(2^-7)
	0.0039062301319669,  // atan(2^-8)
	0.0019531225164788,  // atan(2^-9)
	0.0009765621895593,  // atan(2^-10)
	0.0004882812111949,  // atan(2^-11)
	0.0002441406201494,  // atan(2^-12)
	0.0001220703118937,  // atan(2^-13)
	0.0000610351561742,  // atan(2^-14)
	0.0000305175781155   // atan(2^-15)
};

// Scaling factor to adjust the results after the iterations
static const double cordic_scale = 0.6072529350088813;

// Function to compute sine and cosine using the CORDIC algorithm
void trig_cordic(double theta, double* cosine, double* sine) {
	double x = cordic_scale;  // Initial x component (scaled)
	double y = 0.0;           // Initial y component
	double z = theta;         // Remaining angle to rotate

	// Iterative rotation using precomputed arctangents
	for (int i = 0; i < CORDIC_ITERATIONS; i++) {
		double x_new, y_new;
		double atan_i = cordic_atan_table[i];
		double shift = (1 << i);  // 2^i, using bit shifts for efficiency

		// Rotation direction depends on the sign of z
		if (z >= 0) {
			x_new = x - (y / shift);
			y_new = y + (x / shift);
			z -= atan_i;
		} else {
			x_new = x + (y / shift);
			y_new = y - (x / shift);
			z += atan_i;
		}

		x = x_new;
		y = y_new;
	}

	*cosine = x;  // x converges to cos(theta)
	*sine = y;    // y converges to sin(theta)
}

double sin(double theta) {
	double sine, cosine;
	cordic(theta, &cosine, &sine);
	return sine;
}

double cos(double theta) {
	double sine, cosine;
	cordic(theta, &cosine, &sine);
	return cosine;
}

#endif