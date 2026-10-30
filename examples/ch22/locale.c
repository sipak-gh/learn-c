/* locale.c - which locales can a program switch to? */
#include <locale.h>
#include <stdio.h>

static void try(const char *name)
{
    const char *r = setlocale(LC_ALL, name);
    printf("setlocale(LC_ALL, \"%s\") -> %s\n", name, r ? r : "NULL (failed)");
}

int main(void)
{
    printf("at start-up: %s\n", setlocale(LC_ALL, NULL));
    try("C.UTF-8");
    try("en_US.UTF-8");
    try("xx_YY");                      /* a locale nobody has */
    try("");                           /* the one from LANG / LC_ALL */
    return 0;
}
