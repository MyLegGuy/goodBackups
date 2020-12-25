#define SEPARATOR '/'
#ifndef linux
	#warning forward slash is used for directory separator
#endif
void removeNewline(char* _toRemove);
