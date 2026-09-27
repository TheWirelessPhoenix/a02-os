/* Minimal freestanding C library for A02-OS. */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, const void *s, size_t n)
{
	uint8_t *dp = d;
	const uint8_t *sp = s;
	if (!(((uintptr_t)dp | (uintptr_t)sp | n) & 3)) {
		uint32_t *dw = (uint32_t *)dp;
		const uint32_t *sw = (const uint32_t *)sp;
		for (n >>= 2; n; n--)
			*dw++ = *sw++;
		return d;
	}
	while (n--)
		*dp++ = *sp++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	uint8_t *dp = d;
	const uint8_t *sp = s;
	if (dp < sp)
		while (n--)
			*dp++ = *sp++;
	else
		while (n--)
			dp[n] = sp[n];
	return d;
}

void *memset(void *d, int c, size_t n)
{
	uint8_t *dp = d;
	while (n--)
		*dp++ = (uint8_t)c;
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *x = a, *y = b;
	for (; n; n--, x++, y++)
		if (*x != *y)
			return *x - *y;
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}

char *strchr(const char *s, int c)
{
	for (; *s; s++)
		if (*s == (char)c)
			return (char *)s;
	return c ? NULL : (char *)s;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++))
		;
	return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; i++)
		d[i] = s[i];
	for (; i < n; i++)
		d[i] = 0;
	return d;
}

/* last path component for titles ("/" -> "SD CARD") */
const char *strrchr_last(const char *path)
{
	const char *p = path, *last = path;
	for (; *p; p++)
		if (*p == '/' && p[1])
			last = p + 1;
	return last == path ? "SD CARD" : last;
}

char *strrchr_last_slash(char *path)
{
	char *last = 0;
	for (; *path; path++)
		if (*path == '/')
			last = path;
	return last;
}
