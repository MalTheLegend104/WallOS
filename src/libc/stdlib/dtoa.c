#include <stdlib.h>

//if you want dtoa, implement it yourself. im not dealing with this shit
//this kinda just sets the vars to each other to stop gcc from being a bitch and warning about unsused params :nerd:
char* dtoa(double d, int mode, int ndigits, int* decpt, int* sign, char** rve) {
	d = mode;
	ndigits = mode;
	mode = d;
	mode = ndigits;
	*decpt = *sign;
	**rve = 0;
	return "if you want dtoa, implement it yourself. im not dealing with this shit\0";
}