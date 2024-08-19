#ifndef CTYPE_H
#define CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

	int toupper(int c);
	int tolower(int c);

	int isprint(int c);
	int isspace(int c);
	int isdigit(int c);
	int isxdigit(int c);

#ifdef __cplusplus
}
#endif

#endif // CTYPE_H