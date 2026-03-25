#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
        int num = 0;

        for (;;) {
                printf("%d\n", num++);
                sleep(1);
        }

        return 0;
}
