#ifndef STDLIB_H
#define STDLIB_H 
#ifdef __cplusplus
extern "C" {
#endif
	char* itoa(long long value, char* buffer, int base);
	char* ftoa(double d, char* buffer, int precision);
    // if you want dtoa, do it yourself im not touching this shit
    char *dtoa(double d, int mode, int ndigits, int *decpt, int *sign, char **rve);

#ifdef __cplusplus
}
#endif

#endif
