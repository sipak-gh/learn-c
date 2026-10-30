/* gets.c - never use gets: it cannot know how big the array is. */
#include <stdio.h>

int main(void)
{
    char name[8];
    gets(name);                   /* removed from C in C11 */
    printf("hello, %s\n", name);
    return 0;
}
