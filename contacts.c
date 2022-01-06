#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    remove("user1");
    remove("user2");
    remove("user3");
    mkdir("user1", 0700);
    mkdir("user2", 0700);
    mkdir("user3", 0700);

    FILE *ptr = fopen("user1/Rubrica.txt", "a+");
    fprintf(ptr, "user2\n");
    fclose(ptr);

    ptr = fopen("user2/Rubrica.txt", "a+");
    fprintf(ptr, "user1\nuser3\n");
    fclose(ptr);

    ptr = fopen("user3/Rubrica.txt", "a+");
    fprintf(ptr, "user2\n");
    fclose(ptr);

    return 0;
}